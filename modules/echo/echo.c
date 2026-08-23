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

	re_snprintf(a, sizeof(a), "A-%p", sess);

	audio_set_devicename(call_audio(sess->call_in), a, a);
	video_set_devicename(call_video(sess->call_in), a, a);

	call_set_handlers(sess->call_in, NULL, call_dtmf_handler, NULL);

	list_append(&sessionl, &sess->le, sess);
	err = ua_answer(ua, call, VIDMODE_ON);

	if (err)
		mem_deref(sess);

	return err;
}


static void event_handler(enum bevent_ev ev, struct bevent *event, void *arg)
{
	int err;
	struct ua   *ua   = bevent_get_ua(event);
	struct call *call = bevent_get_call(event);
	(void)arg;

	switch (ev) {

	case BEVENT_CALL_INCOMING:
		info("echo: CALL_INCOMING: peer=%s  -->  local=%s\n",
				call_peeruri(call),
				call_localuri(call));

		err = new_session(ua, call);
		if (err)
			call_hangup(call, 500, "Server Error");
		break;

	case BEVENT_CALL_CLOSED:
		info("echo: CALL_CLOSED: peer=%s  -->  local=%s\n",
				call_peeruri(call),
				call_localuri(call));

		struct le *le;
		LIST_FOREACH(&sessionl, le)
		{
			struct session *sess = le->data;
			if (sess->call_in == call) {
				mem_deref(sess);
				break;
			}
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

	err = bevent_register(event_handler, 0);
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

	bevent_unregister(event_handler);

	return 0;
}


const struct mod_export DECL_EXPORTS(echo) = {
	"echo",
	"application",
	module_init,
	module_close
};
