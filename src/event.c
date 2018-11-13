/**
 * @file src/event.c  Baresip event handling
 *
 * Copyright (C) 2017 Creytiv.com
 */

#include <re.h>
#include <baresip.h>
#include "core.h"


static const char *event_class_name(enum ua_event ev)
{
	switch (ev) {

	case UA_EVENT_REGISTERING:
	case UA_EVENT_REGISTER_OK:
	case UA_EVENT_REGISTER_FAIL:
	case UA_EVENT_UNREGISTERING:
		return "register";

	case UA_EVENT_MWI_NOTIFY:
		return "mwi";

	case UA_EVENT_SHUTDOWN:
	case UA_EVENT_EXIT:
		return "application";

	case UA_EVENT_CALL_INCOMING:
	case UA_EVENT_CALL_RINGING:
	case UA_EVENT_CALL_PROGRESS:
	case UA_EVENT_CALL_ESTABLISHED:
	case UA_EVENT_CALL_CLOSED:
	case UA_EVENT_CALL_TRANSFER:
	case UA_EVENT_CALL_TRANSFER_FAILED:
	case UA_EVENT_CALL_DTMF_START:
	case UA_EVENT_CALL_DTMF_END:
	case UA_EVENT_CALL_RTCP:
	case UA_EVENT_CALL_MENC:
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


int event_encode_dict(struct odict *od, struct ua *ua, enum ua_event ev,
		      struct call *call, const char *prm)
{
	const char *event_str = uag_event_str(ev);
	int err = 0;

	if (!od)
		return EINVAL;

	err |= odict_entry_add(od, "type", ODICT_STRING, event_str);
	err |= odict_entry_add(od, "class",
			       ODICT_STRING, event_class_name(ev));

	if (ua) {
		err |= odict_entry_add(od, "accountaor",
				       ODICT_STRING, ua_aor(ua));
	}

	if (err)
		goto out;

	if (call) {

		const char *dir;

		dir = call_is_outgoing(call) ? "outgoing" : "incoming";

		err |= odict_entry_add(od, "direction", ODICT_STRING, dir);
		err |= odict_entry_add(od, "peeruri",
				       ODICT_STRING, call_peeruri(call));
		err |= odict_entry_add(od, "id", ODICT_STRING, call_id(call));
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
#ifdef USE_VIDEO
		else if (0 == str_casecmp(prm, "video"))
			strm = video_strm(call_video(call));
#endif

		err = add_rtcp_stats(od, stream_rtcp_stats(strm));
		if (err)
			goto out;
	}

 out:

	return err;
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
	case UA_EVENT_UNREGISTERING:        return "UNREGISTERING";
	case UA_EVENT_MWI_NOTIFY:           return "MWI_NOTIFY";
	case UA_EVENT_SHUTDOWN:             return "SHUTDOWN";
	case UA_EVENT_EXIT:                 return "EXIT";
	case UA_EVENT_CALL_INCOMING:        return "CALL_INCOMING";
	case UA_EVENT_CALL_RINGING:         return "CALL_RINGING";
	case UA_EVENT_CALL_PROGRESS:        return "CALL_PROGRESS";
	case UA_EVENT_CALL_ESTABLISHED:     return "CALL_ESTABLISHED";
	case UA_EVENT_CALL_CLOSED:          return "CALL_CLOSED";
	case UA_EVENT_CALL_TRANSFER:        return "TRANSFER";
	case UA_EVENT_CALL_TRANSFER_FAILED: return "TRANSFER_FAILED";
	case UA_EVENT_CALL_DTMF_START:      return "CALL_DTMF_START";
	case UA_EVENT_CALL_DTMF_END:        return "CALL_DTMF_END";
	case UA_EVENT_CALL_RTCP:            return "CALL_RTCP";
	case UA_EVENT_CALL_MENC:            return "CALL_MENC";
	case UA_EVENT_VU_TX:                return "VU_TX_REPORT";
	case UA_EVENT_VU_RX:                return "VU_RX_REPORT";
	case UA_EVENT_AUDIO_ERROR:          return "AUDIO_ERROR";
	default: return "?";
	}
}
