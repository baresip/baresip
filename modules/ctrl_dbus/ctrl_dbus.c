/**
 * @file ctrl_dbus.c  DBUS interface for baresip
 *
 * Communication channel to control and monitor Baresip via DBUS.
 *
 * It receives commands to be executed and notifies about events.
 *
 * DBUS slots:
 * invoke
 * - command (string) : Command to be executed with appended parameters. Use a
 *   blank to separate the command from its parameters! Multiple parameters are
 *   often separated by commas. But this depends on the implementation of the
 *   command.
 *
 * Returns:
 * - response (string) : The response of the command. Numbers will be converted
 *   to a string. Bool will be converted to "true"/"false".
 *
 * Command example:
 *
 \verbatim
 # With qdbus of Qt.
 qdbus com.github.Baresip /baresip com.github.Baresip.invoke reginfo

 # With gdbus of GLib.
 gdbus call -e -d com.github.Baresip -o /baresip \
	-m com.github.Baresip.invoke reginfo

 # With busctl of systemd.
 busctl --user call com.github.Baresip /baresip com.github.Baresip \
	invoke s "reginfo"
 \endverbatim
 *
 *
 * Baresip UA events will be converted to DBUS
 * signals:
 *
 * - class     : Event class.
 * - type      : Event ID.
 * - param     : Specific event information. For UA-events param is a JSON
 *   encoded string representation of the UA-event.
 *
 *
 * Example of an UA-event
 *
 * class = "call"
 * type = "CALL_CLOSED"
 * param =
 \verbatim
 {
  "event"      : "true",
  "class"      : "call",
  "type"       : "CALL_CLOSED",
  "param"      : "Connection reset by peer",
  "accountaor" : "sip:alice@atlanta.com",
  "direction"  : "incoming",
  "peeruri"    : "sip:bob@biloxy.com",
  "id"         : "73a12546589651f8"
 }
 \endverbatim
 *
 * SIP messages are converted to DBUS signals
 *
 *
 * Copyright (C) 2020 commend.com - Christian Spielberger
 */

#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <re.h>
#include <re_atomic.h>
#include <baresip.h>
#include "baresipbus.h"


/**
 * @defgroup ctrl_dbus ctrl_dbus
 *
 */


struct ctrl_st {
	thrd_t thrd;                /**< Thread for GMainLoop    */
	GMainLoop *loop;            /**< Main loop               */
	RE_ATOMIC bool run;         /**< Main loop run flag      */

	guint bus_owner;            /**< Handle of dbus owner    */
	DBusBaresip *interface;     /**< dbus interface          */

	char *command;              /**< Current command                     */
	struct mqueue *mqueue;      /**< Queue processed in main thread      */
	struct mbuf *mb;            /**< Command response buffer             */

	struct {
		mtx_t mtx;
		cnd_t cnd;
	} wait;
};

static struct ctrl_st *m_st = NULL;

static int print_handler(const char *p, size_t size, void *arg)
{
	struct mbuf *mb = arg;

	return mbuf_write_mem(mb, (uint8_t *)p, size);
}


static void command_handler(void *data, void *arg)
{
	struct ctrl_st *st = arg;

	(void) data;
	if (!str_isset(st->command))
		goto out;

	st->mb = mbuf_alloc(128);
	struct re_printf pf = {print_handler, st->mb};
	int err;
	size_t len = strlen(st->command);

	if (len == 1) {
		/* Relay message to key commands */
		err = cmd_process(baresip_commands(),
					   NULL,
					   st->command[0],
					   &pf, NULL);
	}
	else {
		/* Relay message to long commands */
		err = cmd_process_long(baresip_commands(),
					   st->command,
					   len,
					   &pf, NULL);
	}

	if (err)
		warning("ctrl_dbus: error processing command \"%s\" (%m)\n",
			st->command, err);

	mbuf_set_pos(st->mb, 0);

out:
	mtx_lock(&st->wait.mtx);
	st->command = mem_deref(st->command);
	cnd_signal(&st->wait.cnd);
	mtx_unlock(&st->wait.mtx);
}


struct modev {
	char *event;
	char *txt;
};


static void modev_destructor(void *arg)
{
	struct modev *d = arg;

	mem_deref(d->event);
	mem_deref(d->txt);
}


static void send_event(void *data, void *arg)
{
	struct modev *modev = data;
	(void)arg;

	module_event("ctrl_dbus", modev->event, NULL, NULL, "%s", modev->txt);
	mem_deref(modev);
}


static void queue_handler(int id, void *data, void *arg)
{
	switch (id) {
		case 0:
			command_handler(data, arg);
			break;
		case 1:
			send_event(data, arg);
			break;
		default:
			break;
	}
}


static gboolean
on_handle_invoke(DBusBaresip *interface,
		GDBusMethodInvocation *invocation,
		const gchar *command,
		gpointer arg)
{
	char *response = "";
	struct ctrl_st *st = arg;
	int err;

	err = str_dup(&st->command, command);
	if (err)
		return false;

	mtx_lock(&st->wait.mtx);
	err = mqueue_push(st->mqueue, 0, NULL);
	while (!err && st->command)
		cnd_wait(&st->wait.cnd, &st->wait.mtx);

	mtx_unlock(&st->wait.mtx);

	if (!err && st->mb) {
		err = mbuf_strdup(st->mb, &response, mbuf_get_left(st->mb));
		if (err)
			warning("ctrl_dbus: could not allocate response (%m)",
					err);

		dbus_baresip_complete_invoke(interface, invocation, response);
		mem_deref(response);
		st->mb = mem_deref(st->mb);
	}
	else {
		dbus_baresip_complete_invoke(interface, invocation,
				err ? "invoke failed" : "");
	}

	return true;
}


/*
 * Relay UA events
 */
static void event_handler(enum bevent_ev ev, struct bevent *event, void *arg)
{
	struct ctrl_st *st = arg;
	struct mbuf *buf;
	struct re_printf pf;
	struct odict *od = NULL;
	int err = 0;
	const char *class;
	const char *etype = bevent_str(ev);

	if (!st->interface)
		return;

	buf = mbuf_alloc(192);
	err = odict_alloc(&od, 8);
	if (!buf || err)
		goto out;

	pf.vph = print_handler;
	pf.arg = buf;
	err = odict_encode_bevent(od, event);
	if (err)
		goto out;

	class = odict_string(od, "class");
	err = json_encode_odict(&pf, od);
	if (err) {
		warning("ctrl_dbus: failed to encode json (%m)\n", err);
		goto out;
	}

	mbuf_write_u8(buf, 0);
	mbuf_set_pos(buf, 0);
	dbus_baresip_emit_event(st->interface, class ? class : "other", etype,
			(const char *) mbuf_buf(buf));

 out:
	mem_deref(buf);
	mem_deref(od);
}


static void message_handler(struct ua *ua, const struct pl *peer,
			    const struct pl *ctype,
			    struct mbuf *body, void *arg)
{
	struct ctrl_st *st = arg;
	char *buf1 = NULL;
	char *buf2 = NULL;
	char *buf3 = NULL;
	size_t pos = 0;
	const char *aor = account_aor(ua_account(ua));
	int err = 0;

	if (!st->interface)
		return;

	err |= pl_strdup(&buf1, peer);
	err |= pl_strdup(&buf2, ctype);
	if (body) {
		pos = body->pos;
		err |= mbuf_strdup(body, &buf3, mbuf_get_left(body));
		body->pos = pos;
	}

	if (err) {
		warning("ctrl_dbus: failed to convert SIP message (%m)\n",
				err);
		goto out;
	}

	dbus_baresip_emit_message(st->interface,
			aor ? aor : "",
			buf1, buf2, buf3);

out:
	mem_deref(buf1);
	mem_deref(buf2);
	mem_deref(buf3);
}


static void ctrl_destructor(void *arg)
{
	struct ctrl_st *st = arg;
	if (re_atomic_rlx(&st->run)) {
		re_atomic_rlx_set(&st->run, false);
		g_main_loop_quit(st->loop);
		thrd_join(st->thrd, NULL);
	}

	if (st == m_st)
		m_st = NULL;

	if (st->bus_owner) {
		g_bus_unown_name(st->bus_owner);
	}

	if (st->interface)
		g_object_unref (st->interface);

	g_main_loop_unref(st->loop);

	mtx_destroy(&st->wait.mtx);
	cnd_destroy(&st->wait.cnd);
	mem_deref(st->mqueue);
}


static int thread(void *arg)
{
	struct ctrl_st *st = arg;

	while (re_atomic_rlx(&st->run))
		g_main_loop_run(st->loop);

	return 0;
}


static int ctrl_alloc(struct ctrl_st **stp)
{
	struct ctrl_st *st;
	int err = 0;

	if (!stp)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), ctrl_destructor);
	if (!st)
		return ENOMEM;

	err = mtx_init(&st->wait.mtx, mtx_plain) != thrd_success;
	if (err) {
		err = ENOMEM;
		goto out;
	}
	cnd_init(&st->wait.cnd);

	st->loop = g_main_loop_new(NULL, false);
	if (!st->loop) {
		err = ENOMEM;
		goto out;
	}

	err  = mqueue_alloc(&st->mqueue, queue_handler, st);
	if (err)
		goto out;

	re_atomic_rlx_set(&st->run, true);
	err = thread_create_name(&st->thrd, "ctrl_dbus", thread, st);
	if (err)
		re_atomic_rlx_set(&st->run, false);

out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}


static void
on_name_acquired(GDBusConnection *connection, const gchar *name, gpointer arg)
{
	GError *error;
	struct ctrl_st *st = arg;
	struct modev *modev;
	const char fmt[] = "dbus interface %s exported";
	int err;

	st->interface = dbus_baresip_skeleton_new();
	g_signal_connect (st->interface, "handle-invoke",
			G_CALLBACK (on_handle_invoke), st);
	error = NULL;
	if (!g_dbus_interface_skeleton_export(
				G_DBUS_INTERFACE_SKELETON (st->interface),
				connection, "/baresip", &error)) {
		warning("ctrl_dbus: dbus interface could not be exported\n");
		g_error_free (error);
	}

	info("ctrl_dbus: dbus interface %s exported\n", name);

	modev = mem_zalloc(sizeof(*modev), modev_destructor);
	if (!modev)
		return;

	err  = str_dup(&modev->event, "exported");
	err |= re_sdprintf(&modev->txt, fmt, name);
	if (err)
		return;

	(void)mqueue_push(st->mqueue, 1, modev);
}


static void on_bus_aquired (GDBusConnection *connection,
                                      const gchar     *name,
                                      gpointer         arg)
{
	(void) connection;
	(void) arg;
	info("ctrl_dbus: bus aquired name=%s\n", name);
}


static void on_name_lost (GDBusConnection *connection,
                                      const gchar     *name,
                                      gpointer         arg)
{
	struct ctrl_st *st = arg;
	info("ctrl_dbus: dbus name lost %s\n", name);
	if (!st->interface)
		warning("ctrl_dbus: could not export dbus interface\n");
}


static int ctrl_init(void)
{
	int err;
	const char *name;
	const char syst[] = "system";
	struct pl use = {syst, sizeof(syst)};

	err = ctrl_alloc(&m_st);
	if (err)
		goto outerr;

	err = bevent_register(event_handler, m_st);
	if (err)
		goto outerr;

	err = message_listen(baresip_message(), message_handler, m_st);
	if (err)
		goto outerr;

	(void)conf_get(conf_cur(), "ctrl_dbus_use", &use);
	name = dbus_baresip_interface_info()->name;
	m_st->bus_owner = g_bus_own_name(
			!pl_strcmp(&use, "session") ?
				G_BUS_TYPE_SESSION : G_BUS_TYPE_SYSTEM,
			name, G_BUS_NAME_OWNER_FLAGS_NONE, on_bus_aquired,
			on_name_acquired, on_name_lost, m_st, NULL);

	if (!m_st->bus_owner) {
		warning("ctrl_dbus: could not acquire %s on the %r-bus\n",
				name, &use);
		err = EINVAL;
		goto outerr;
	}

	info("ctrl_dbus: name %s acquired on the %r-bus bus_owner=%u\n",
			name, &use, m_st->bus_owner);
	return 0;

outerr:
	mem_deref(m_st);
	m_st = NULL;
	return err;
}


static int ctrl_close(void)
{
	bevent_unregister(event_handler);
	message_unlisten(baresip_message(), message_handler);
	m_st = mem_deref(m_st);
	return 0;
}


const struct mod_export DECL_EXPORTS(ctrl_dbus) = {
	"ctrl_dbus",
	"application",
	ctrl_init,
	ctrl_close
};
