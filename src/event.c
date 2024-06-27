/**
 * @file src/event.c  Baresip event handling
 *
 * Copyright (C) 2017 Alfred E. Heggestad
 */

#include <re.h>
#include <baresip.h>
#include "core.h"


enum {
	EVENT_MAXSZ = 4096,
};


struct ua_eh {
	struct le le;
	ua_event_h *h;
	void *arg;
};


static struct list ehl;               /**< Event handlers (struct ua_eh)   */


static void eh_destructor(void *arg)
{
	struct ua_eh *eh = arg;

	list_unlink(&eh->le);
}


static const char *event_class_name(enum ua_event ev)
{
	switch (ev) {

	case UA_EVENT_REGISTERING:
	case UA_EVENT_REGISTER_OK:
	case UA_EVENT_REGISTER_FAIL:
	case UA_EVENT_UNREGISTERING:
	case UA_EVENT_FALLBACK_OK:
	case UA_EVENT_FALLBACK_FAIL:
		return "register";

	case UA_EVENT_MWI_NOTIFY:
		return "mwi";

	case UA_EVENT_CREATE:
	case UA_EVENT_SHUTDOWN:
	case UA_EVENT_EXIT:
		return "application";

	case UA_EVENT_CALL_INCOMING:
	case UA_EVENT_CALL_OUTGOING:
	case UA_EVENT_CALL_RINGING:
	case UA_EVENT_CALL_PROGRESS:
	case UA_EVENT_CALL_ANSWERED:
	case UA_EVENT_CALL_ESTABLISHED:
	case UA_EVENT_CALL_CLOSED:
	case UA_EVENT_CALL_TRANSFER:
	case UA_EVENT_CALL_TRANSFER_FAILED:
	case UA_EVENT_CALL_REDIRECT:
	case UA_EVENT_CALL_DTMF_START:
	case UA_EVENT_CALL_DTMF_END:
	case UA_EVENT_CALL_RTPESTAB:
	case UA_EVENT_CALL_RTCP:
	case UA_EVENT_CALL_MENC:
	case UA_EVENT_CALL_LOCAL_SDP:
	case UA_EVENT_CALL_REMOTE_SDP:
	case UA_EVENT_CALL_HOLD:
	case UA_EVENT_CALL_RESUME:
		return "call";
	case UA_EVENT_VU_RX:
	case UA_EVENT_VU_TX:
		return "VU_REPORT";

	default:
		return "other";
	}
}


static int add_rtcp_stats(struct odict *od_parent, const struct rtcp_stats *rs)
{
	struct odict *od = NULL, *tx = NULL, *rx = NULL;
	int err = 0;

	if (!od_parent || !rs)
		return EINVAL;

	err  = odict_alloc(&od, 8);
	err |= odict_alloc(&tx, 8);
	err |= odict_alloc(&rx, 8);
	if (err)
		goto out;

	err  = odict_entry_add(tx, "sent", ODICT_INT, (int64_t)rs->tx.sent);
	err |= odict_entry_add(tx, "lost", ODICT_INT, (int64_t)rs->tx.lost);
	err |= odict_entry_add(tx, "jit", ODICT_INT, (int64_t)rs->tx.jit);
	if (err)
		goto out;

	err  = odict_entry_add(rx, "sent", ODICT_INT, (int64_t)rs->rx.sent);
	err |= odict_entry_add(rx, "lost", ODICT_INT, (int64_t)rs->rx.lost);
	err |= odict_entry_add(rx, "jit", ODICT_INT, (int64_t)rs->rx.jit);
	if (err)
		goto out;

	err  = odict_entry_add(od, "tx", ODICT_OBJECT, tx);
	err |= odict_entry_add(od, "rx", ODICT_OBJECT, rx);
	err |= odict_entry_add(od, "rtt", ODICT_INT, (int64_t)rs->rtt);
	if (err)
		goto out;

	/* add object to the parent */
	err = odict_entry_add(od_parent, "rtcp_stats", ODICT_OBJECT, od);
	if (err)
		goto out;

 out:
	mem_deref(od);
	mem_deref(tx);
	mem_deref(rx);

	return err;
}


/**
 * Encode an event to a dictionary
 *
 * @param od   Dictionary to encode into
 * @param ua   User-Agent
 * @param ev   Event type
 * @param call Call object (optional)
 * @param prm  Event parameters
 *
 * @return 0 if success, otherwise errorcode
 */
int event_encode_dict(struct odict *od, struct ua *ua, enum ua_event ev,
		      struct call *call, const char *prm)
{
	const char *event_str = uag_event_str(ev);
	struct sdp_media *amedia;
	struct sdp_media *vmedia;
	int err = 0;

	if (!od)
		return EINVAL;

	err |= odict_entry_add(od, "type", ODICT_STRING, event_str);
	err |= odict_entry_add(od, "class",
			       ODICT_STRING, event_class_name(ev));

	if (ua) {
		err |= odict_entry_add(od, "accountaor",
				       ODICT_STRING,
				       account_aor(ua_account(ua)));
	}

	if (err)
		goto out;

	if (call) {

		const char *dir;
		const char *call_identifier;
		const char *peerdisplayname;
		enum sdp_dir ardir;
		enum sdp_dir vrdir;
		enum sdp_dir aldir;
		enum sdp_dir vldir;
		enum sdp_dir adir;
		enum sdp_dir vdir;

		dir = call_is_outgoing(call) ? "outgoing" : "incoming";

		err |= odict_entry_add(od, "direction", ODICT_STRING, dir);
		err |= odict_entry_add(od, "peeruri",
				       ODICT_STRING, call_peeruri(call));
		peerdisplayname = call_peername(call);
		if (peerdisplayname){
				err |= odict_entry_add(od, "peerdisplayname",
						ODICT_STRING, peerdisplayname);
		}
		call_identifier = call_id(call);
		if (call_identifier) {
			err |= odict_entry_add(od, "id", ODICT_STRING,
						   call_identifier);
		}

		amedia = stream_sdpmedia(audio_strm(call_audio(call)));
		ardir = sdp_media_rdir(amedia);
		aldir  = sdp_media_ldir(amedia);
		adir  = sdp_media_dir(amedia);
		if (!sa_isset(sdp_media_raddr(amedia), SA_ADDR))
			ardir = aldir = adir = SDP_INACTIVE;

		vmedia = stream_sdpmedia(video_strm(call_video(call)));
		vrdir = sdp_media_rdir(vmedia);
		vldir = sdp_media_ldir(vmedia);
		vdir  = sdp_media_dir(vmedia);
		if (!sa_isset(sdp_media_raddr(vmedia), SA_ADDR))
			vrdir = vldir = vdir = SDP_INACTIVE;

		err |= odict_entry_add(od, "remoteaudiodir", ODICT_STRING,
				sdp_dir_name(ardir));
		err |= odict_entry_add(od, "remotevideodir", ODICT_STRING,
				sdp_dir_name(vrdir));
		err |= odict_entry_add(od, "audiodir", ODICT_STRING,
				sdp_dir_name(adir));
		err |= odict_entry_add(od, "videodir", ODICT_STRING,
				sdp_dir_name(vdir));
		err |= odict_entry_add(od, "localaudiodir", ODICT_STRING,
				sdp_dir_name(aldir));
		err |= odict_entry_add(od, "localvideodir", ODICT_STRING,
				sdp_dir_name(vldir));
		if (call_diverteruri(call))
			err |= odict_entry_add(od, "diverteruri", ODICT_STRING,
					       call_diverteruri(call));

		const char *user_data = call_user_data(call);
		if (user_data) {
			err |= odict_entry_add(od, "userdata", ODICT_STRING,
				user_data);
		}

		if (err)
			goto out;
	}

	if (str_isset(prm)) {
		err = odict_entry_add(od, "param", ODICT_STRING, prm);
		if (err)
			goto out;
	}

	if (ev == UA_EVENT_CALL_RTCP) {
		struct stream *strm = NULL;

		if (0 == str_casecmp(prm, "audio"))
			strm = audio_strm(call_audio(call));
		else if (0 == str_casecmp(prm, "video"))
			strm = video_strm(call_video(call));

		err = add_rtcp_stats(od, stream_rtcp_stats(strm));
		if (err)
			goto out;
	}

 out:

	return err;
}


/**
 * Add audio buffer status
 *
 * @param od_parent  Dictionary to encode into
 * @param call       Call object
 *
 * @return 0 if success, otherwise errorcode
 */
int event_add_au_jb_stat(struct odict *od_parent, const struct call *call)
{
	int err = 0;
	err = odict_entry_add(od_parent, "audio_jb_ms",ODICT_INT,
			    (int64_t)audio_jb_current_value(call_audio(call)));
	return err;
}


/**
 * Register a User-Agent event handler
 *
 * @param h   Event handler
 * @param arg Handler argument
 *
 * @return 0 if success, otherwise errorcode
 */
int uag_event_register(ua_event_h *h, void *arg)
{
	struct ua_eh *eh;

	if (!h)
		return EINVAL;

	uag_event_unregister(h);

	eh = mem_zalloc(sizeof(*eh), eh_destructor);
	if (!eh)
		return ENOMEM;

	eh->h = h;
	eh->arg = arg;

	list_append(&ehl, &eh->le, eh);

	return 0;
}


/**
 * Unregister a User-Agent event handler
 *
 * @param h   Event handler
 */
void uag_event_unregister(ua_event_h *h)
{
	struct le *le;

	for (le = ehl.head; le; le = le->next) {

		struct ua_eh *eh = le->data;

		if (eh->h == h) {
			mem_deref(eh);
			break;
		}
	}
}


/**
 * Send a User-Agent event to all UA event handlers
 *
 * @param ua   User-Agent object (optional)
 * @param ev   User-agent event
 * @param call Call object (optional)
 * @param fmt  Formatted arguments
 * @param ...  Variable arguments
 */
void ua_event(struct ua *ua, enum ua_event ev, struct call *call,
	      const char *fmt, ...)
{
	struct le *le;
	char buf[256];
	va_list ap;

	va_start(ap, fmt);
	(void)re_vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	/* send event to all clients */
	le = ehl.head;
	while (le) {
		struct ua_eh *eh = le->data;
		le = le->next;

		if (call_is_evstop(call)) {
			call_set_evstop(call, false);
			break;
		}

		eh->h(ua, ev, call, buf, eh->arg);
	}
}


/**
 * Send a UA_EVENT_MODULE event with a general format for modules
 *
 * @param module Module name
 * @param event  Event name
 * @param ua     User-Agent object (optional)
 * @param call   Call object (optional)
 * @param fmt    Formatted arguments
 * @param ...    Variable arguments
 */
void module_event(const char *module, const char *event, struct ua *ua,
		struct call *call, const char *fmt, ...)
{
	struct le *le;
	char *buf;
	char *p;
	size_t len = EVENT_MAXSZ;
	va_list ap;

	if (!module || !event)
		return;

	buf = mem_zalloc(EVENT_MAXSZ, NULL);
	if (!buf)
		return;

	if (-1 == re_snprintf(buf, len, "%s,%s,", module, event))
		goto out;

	p = buf + str_len(buf);
	len -= str_len(buf);

	va_start(ap, fmt);
	(void)re_vsnprintf(p, len, fmt, ap);
	va_end(ap);

	/* send event to all clients */
	le = ehl.head;
	while (le) {
		struct ua_eh *eh = le->data;
		le = le->next;

		eh->h(ua, UA_EVENT_MODULE, call, buf, eh->arg);
	}

out:
	mem_deref(buf);
}


/**
 * Get the name of the User-Agent event
 *
 * @param ev User-Agent event
 *
 * @return Name of the event
 */
const char *uag_event_str(enum ua_event ev)
{
	switch (ev) {

	case UA_EVENT_REGISTERING:          return "REGISTERING";
	case UA_EVENT_REGISTER_OK:          return "REGISTER_OK";
	case UA_EVENT_REGISTER_FAIL:        return "REGISTER_FAIL";
	case UA_EVENT_FALLBACK_OK:          return "FALLBACK_OK";
	case UA_EVENT_FALLBACK_FAIL:        return "FALLBACK_FAIL";
	case UA_EVENT_UNREGISTERING:        return "UNREGISTERING";
	case UA_EVENT_MWI_NOTIFY:           return "MWI_NOTIFY";
	case UA_EVENT_CREATE:               return "CREATE";
	case UA_EVENT_SHUTDOWN:             return "SHUTDOWN";
	case UA_EVENT_EXIT:                 return "EXIT";
	case UA_EVENT_CALL_INCOMING:        return "CALL_INCOMING";
	case UA_EVENT_CALL_OUTGOING:        return "CALL_OUTGOING";
	case UA_EVENT_CALL_RINGING:         return "CALL_RINGING";
	case UA_EVENT_CALL_PROGRESS:        return "CALL_PROGRESS";
	case UA_EVENT_CALL_ANSWERED:        return "CALL_ANSWERED";
	case UA_EVENT_CALL_ESTABLISHED:     return "CALL_ESTABLISHED";
	case UA_EVENT_CALL_CLOSED:          return "CALL_CLOSED";
	case UA_EVENT_CALL_TRANSFER:        return "TRANSFER";
	case UA_EVENT_CALL_TRANSFER_FAILED: return "TRANSFER_FAILED";
	case UA_EVENT_CALL_REDIRECT:        return "CALL_REDIRECT";
	case UA_EVENT_CALL_DTMF_START:      return "CALL_DTMF_START";
	case UA_EVENT_CALL_DTMF_END:        return "CALL_DTMF_END";
	case UA_EVENT_CALL_RTPESTAB:        return "CALL_RTPESTAB";
	case UA_EVENT_CALL_RTCP:            return "CALL_RTCP";
	case UA_EVENT_CALL_MENC:            return "CALL_MENC";
	case UA_EVENT_VU_TX:                return "VU_TX_REPORT";
	case UA_EVENT_VU_RX:                return "VU_RX_REPORT";
	case UA_EVENT_AUDIO_ERROR:          return "AUDIO_ERROR";
	case UA_EVENT_CALL_LOCAL_SDP:       return "CALL_LOCAL_SDP";
	case UA_EVENT_CALL_REMOTE_SDP:      return "CALL_REMOTE_SDP";
	case UA_EVENT_CALL_HOLD:            return "CALL_HOLD";
	case UA_EVENT_CALL_RESUME:          return "CALL_RESUME";
	case UA_EVENT_REFER:                return "REFER";
	case UA_EVENT_MODULE:               return "MODULE";
	case UA_EVENT_END_OF_FILE:          return "END_OF_FILE";
	case UA_EVENT_CUSTOM:               return "CUSTOM";
	default: return "?";
	}
}
