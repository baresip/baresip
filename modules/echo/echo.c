/**
 * @file echo.c Echo module
 */
#include <re.h>
#include <baresip.h>

/**
 *
 * Multi Call Echo module
 *
 * REQUIRES: aubridge
 * NOTE: This module is experimental.
 *
 */

struct session {
	struct le le;
	struct call *call_in;
};


static struct list sessionl;


static void destructor(void *arg)
{
	struct session *sess = arg;

	debug("echo: session destroyed\n");

	list_unlink(&sess->le);
}


static void call_event_handler(struct call *call, enum call_event ev,
			       const char *str, void *arg)
{
	struct session *sess = arg;
	(void)call;

	switch (ev) {

	case CALL_EVENT_CLOSED:
		debug("echo: CALL_CLOSED: %s\n", str);
		mem_deref(sess->call_in);
		mem_deref(sess);
		break;

	default:
		break;
	}
}


static void call_dtmf_handler(struct call *call, char key, void *arg)
{
	(void)arg;

	debug("echo: relaying DTMF event: key = '%c'\n", key ? key : '.');

	call_send_digit(call, key);
}


static int new_session(struct ua *ua, struct call *call)
{
	struct session *sess;
	char a[64];
	int err = 0;

	sess = mem_zalloc(sizeof(*sess), destructor);
	if (!sess)
		return ENOMEM;

	sess->call_in = call;

	re_snprintf(a, sizeof(a), "A-%x", sess);

	audio_set_devicename(call_audio(sess->call_in), a, a);
	video_set_devicename(call_video(sess->call_in), a, a);

	call_set_handlers(sess->call_in, call_event_handler,
			call_dtmf_handler, sess);

	list_append(&sessionl, &sess->le, sess);
	err = ua_answer(ua, call, VIDMODE_ON);

	if (err)
		mem_deref(sess);

	return err;
}


static void ua_event_handler(struct ua *ua, enum ua_event ev,
		struct call *call, const char *prm, void *arg)
{
	int err;
	(void)prm;
	(void)arg;

	switch (ev) {

	case UA_EVENT_CALL_INCOMING:
		info("echo: CALL_INCOMING: peer=%s  -->  local=%s\n",
				call_peeruri(call),
				call_localuri(call));

		err = new_session(ua, call);
		if (err) {
			call_hangup(call, 500, "Server Error");
		}
		break;

	default:
		break;
	}
}


static int module_init(void)
{
	int err;

	list_init(&sessionl);

	err = uag_event_register(ua_event_handler, 0);
	if (err)
		return err;

	debug("echo: module loaded\n");

	return 0;
}


static int module_close(void)
{
	debug("echo: module closing..\n");

	if (!list_isempty(&sessionl)) {

		info("echo: flushing %u sessions\n", list_count(&sessionl));
		list_flush(&sessionl);
	}

	uag_event_unregister(ua_event_handler);

	return 0;
}


const struct mod_export DECL_EXPORTS(echo) = {
	"echo",
	"application",
	module_init,
	module_close
};
