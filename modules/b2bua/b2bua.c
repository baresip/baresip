/**
 * @file b2bua.c Back-to-Back User-Agent (B2BUA) module
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <re.h>
#include <baresip.h>


/**
 * @defgroup b2bua b2bua
 *
 * Back-to-Back User-Agent (B2BUA) module
 *
 * NOTE: This module is experimental.
 *
 * N session objects
 * 1 session object has 2 call objects (left, right leg)
 */


struct session {
	struct le le;
	struct call *call_in, *call_out;
};


static struct list sessionl;
static struct ua *ua_in, *ua_out;


static struct call *other_call(struct session *sess, const struct call *call)
{
	if (sess->call_in == call) return sess->call_out;
	if (sess->call_out == call) return sess->call_in;

	return NULL;
}


static void destructor(void *arg)
{
	struct session *sess = arg;

	debug("b2bua: session destroyed (in=%p, out=%p)\n",
	      sess->call_in, sess->call_out);

	list_unlink(&sess->le);
	mem_deref(sess->call_out);
	mem_deref(sess->call_in);
}


static void call_event_handler(struct call *call, enum call_event ev,
			       const char *str, void *arg)
{
	struct session *sess = arg;
	struct call *call2 = other_call(sess, call);

	switch (ev) {

	case CALL_EVENT_ESTABLISHED:
		debug("b2bua: CALL_ESTABLISHED: peer_uri=%s\n",
		      call_peeruri(call));
		ua_answer(call_get_ua(call2), call2);
		break;

	case CALL_EVENT_CLOSED:
		debug("b2bua: CALL_CLOSED: %s\n", str);

		mem_ref(call2);

		ua_hangup(call_get_ua(call2), call2, call_scode(call), "");
		mem_deref(sess);
		break;

	default:
		break;
	}
}


static void call_dtmf_handler(struct call *call, char key, void *arg)
{
	struct session *sess = arg;

	debug("b2bua: relaying DTMF event: key = '%c'\n", key ? key : '.');

	call_send_digit(other_call(sess, call), key);
}


static int new_session(struct call *call)
{
	struct session *sess;
	char a[64], b[64];
	int err;

	sess = mem_zalloc(sizeof(*sess), destructor);
	if (!sess)
		return ENOMEM;

	sess->call_in = call;
	err = ua_connect(ua_out, &sess->call_out, call_peeruri(call),
			 call_localuri(call),
			 call_has_video(call) ? VIDMODE_ON : VIDMODE_OFF);
	if (err) {
		warning("b2bua: ua_connect failed (%m)\n", err);
		goto out;
	}

	re_snprintf(a, sizeof(a), "A-%x", sess);
	re_snprintf(b, sizeof(b), "B-%x", sess);

	/* connect the audio/video-bridge devices */
	audio_set_devicename(call_audio(sess->call_in), a, b);
	audio_set_devicename(call_audio(sess->call_out), b, a);
	video_set_devicename(call_video(sess->call_in), a, b);
	video_set_devicename(call_video(sess->call_out), b, a);

	call_set_handlers(sess->call_in, call_event_handler,
			  call_dtmf_handler, sess);
	call_set_handlers(sess->call_out, call_event_handler,
			  call_dtmf_handler, sess);

	list_append(&sessionl, &sess->le, sess);

 out:
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
		debug("b2bua: CALL_INCOMING: peer=%s  -->  local=%s\n",
		      call_peeruri(call), call_localuri(call));

		err = new_session(call);
		if (err) {
			ua_hangup(ua, call, 500, "Server Error");
		}
		break;

	default:
		break;
	}
}


static int b2bua_status(struct re_printf *pf, void *arg)
{
	struct le *le;
	int err = 0;
	(void)arg;

	err |= re_hprintf(pf, "B2BUA status:\n");
	err |= re_hprintf(pf, "  inbound:  %s\n", ua_aor(ua_in));
	err |= re_hprintf(pf, "  outbound: %s\n", ua_aor(ua_out));

	err |= re_hprintf(pf, "sessions:\n");

	for (le = sessionl.head; le; le = le->next) {

		struct session *sess = le->data;

		err |= re_hprintf(pf, "%-42s  --->  %42s\n",
				  call_peeruri(sess->call_in),
				  call_peeruri(sess->call_out));

		err |= re_hprintf(pf, " %H\n", call_status, sess->call_in);
		err |= re_hprintf(pf, " %H\n", call_status, sess->call_out);
	}

	return err;
}


static const struct cmd cmdv[] = {
	{"b2bua", 0,       0, "b2bua status", b2bua_status },
};


static int module_init(void)
{
	int err;

	ua_in  = uag_find_param("b2bua", "inbound");
	ua_out = uag_find_param("b2bua", "outbound");

	if (!ua_in) {
		warning("b2bua: inbound UA not found\n");
		return ENOENT;
	}
	if (!ua_out) {
		warning("b2bua: outbound UA not found\n");
		return ENOENT;
	}

	err = cmd_register(baresip_commands(), cmdv, ARRAY_SIZE(cmdv));
	if (err)
		return err;

	err = uag_event_register(ua_event_handler, NULL);
	if (err)
		return err;

	/* The inbound UA will handle all non-matching requests */
	ua_set_catchall(ua_in, true);

	debug("b2bua: module loaded\n");

	return 0;
}


static int module_close(void)
{
	debug("b2bua: module closing..\n");

	if (!list_isempty(&sessionl)) {

		info("b2bua: flushing %u sessions\n", list_count(&sessionl));
		list_flush(&sessionl);
	}

	uag_event_unregister(ua_event_handler);
	cmd_unregister(baresip_commands(), cmdv);

	return 0;
}


const struct mod_export DECL_EXPORTS(b2bua) = {
	"b2bua",
	"application",
	module_init,
	module_close
};
