/**
 * @file bundle.c Media Multiplexing Using SDP (BUNDLE)
 *
 * Copyright (C) 2020 Alfred E. Heggestad
 */

#include <re.h>
#include <baresip.h>
#include "core.h"


static const char uri_mid[] = "urn:ietf:params:rtp-hdrext:sdes:mid";


struct bundle {
	struct udp_helper *uh;
	enum bundle_state state;
	uint8_t extmap_mid;         /* Range 1-14  */
};


static void destructor(void *data)
{
	struct bundle *bun = data;

	mem_deref(bun->uh);
}


const char *bundle_state_name(enum bundle_state st)
{
	switch (st) {

	case BUNDLE_NONE: return "None";
	case BUNDLE_BASE: return "Base";
	case BUNDLE_MUX:  return "Mux";
	default: return "???";
	}
}


void bundle_set_state(struct bundle *bun, enum bundle_state st)
{
	if (!bun)
		return;

	debug("bundle: set state: %s\n", bundle_state_name(st));

	bun->state = st;
}


int bundle_alloc(struct bundle **bunp)
{
	struct bundle *bun;

	if (!bunp)
		return EINVAL;

	info("bundle: alloc\n");

	bun = mem_zalloc(sizeof(*bun), destructor);
	if (!bun)
		return ENOMEM;

	*bunp = bun;

	return 0;
}


static bool bundle_handler(const char *name, const char *value, void *arg)
{
	struct list *streaml = arg;
	struct pl pl_mids;
	bool first = true;
	(void)name;

	if (re_regex(value, str_len(value), "BUNDLE[^]+", &pl_mids))
		return false;

	while (pl_mids.l) {

		struct pl ws, pl_mid;
		struct stream *strm;

		if (re_regex(pl_mids.p, pl_mids.l, "[ ]*[0-9a-z]+",
			     &ws, &pl_mid))
			break;

		strm = stream_lookup_mid(streaml, pl_mid.p, pl_mid.l);
		if (!strm) {
			warning("bundle: stream not found (mid=%r)\n",
				&pl_mid);
			return false;
		}

		stream_enable_bundle(strm, first ? BUNDLE_BASE : BUNDLE_MUX);

		pl_advance(&pl_mids, ws.l + pl_mid.l);

		first = false;
	}

	return true;
}


int bundle_sdp_decode(struct sdp_session *sdp, struct list *streaml)
{
	const char *bundle;
	struct le *le;
	int err;

	if (!sdp || !streaml)
		return EINVAL;

	for (le = streaml->head; le; le = le->next) {

		struct stream *strm = le->data;

		stream_parse_mid(strm);
	}

	bundle = sdp_session_rattr_apply(sdp, "group",
					 bundle_handler, streaml);
	if (bundle) {

		err = sdp_session_set_lattr(sdp, true,
					    "group", "%s", bundle);
		if (err)
			return err;
	}

	for (le = streaml->head; le; le = le->next) {

		struct stream *strm = le->data;

		stream_parse_mid(strm);
	}

	return 0;
}


int bundle_set_extmap(struct bundle *bun, struct sdp_media *sdp,
		      uint8_t extmap_mid)
{
	int err;

	if (!sdp || !bun)
		return EINVAL;

	bun->extmap_mid = extmap_mid;

	err = sdp_media_set_lattr(sdp, true, "extmap",
				  "%u %s", bun->extmap_mid, uri_mid);

	return err;
}


static bool extmap_handler(const char *name, const char *value, void *arg)
{
	void **argv = arg;
	struct bundle *bun = argv[0];
	struct sdp_media *sdp = argv[1];
	struct sdp_extmap extmap;
	int err;
	(void)name;

	err = sdp_extmap_decode(&extmap, value);
	if (err) {
		warning("bundle: sdp_extmap_decode error (%m)\n", err);
		return false;
	}

	if (0 == pl_strcasecmp(&extmap.name, uri_mid)) {

		bundle_set_extmap(bun, sdp, extmap.id);

		return true;
	}

	return false;
}


void bundle_handle_extmap(struct bundle *bun, struct sdp_media *sdp)
{
	void *argv[2] = {bun, sdp};

	if (!bun || !sdp)
		return;

	sdp_media_rattr_apply(sdp, "extmap", extmap_handler, argv);
}


static int print_bundle(struct re_printf *pf, const struct list *streaml)
{
	struct le *le;
	int err = 0;

	for (le = list_head(streaml); le; le = le->next) {

		struct stream *strm = le->data;
		const char *mid = stream_mid(strm);

		if (mid)
			err |= re_hprintf(pf, " %s", mid);
	}

	return err;
}


int bundle_sdp_encode(struct sdp_session *sdp, const struct list *streaml)
{
	if (!sdp || !streaml)
		return EINVAL;

	return sdp_session_set_lattr(sdp, true, "group", "BUNDLE%H",
				     print_bundle, streaml);
}


static struct stream *lookup_remote_ssrc(const struct list *streaml,
					 uint32_t ssrc)
{
	struct le *le;

	for (le = streaml->head; le; le = le->next) {
		struct stream *strm = le->data;
		uint32_t rssrc;

		if (stream_ssrc_rx(strm, &rssrc))
			continue;

		if (ssrc == rssrc)
			return strm;
	}

	return NULL;
}


static int get_rtcp_ssrc(const struct rtcp_msg *msg, uint32_t *ssrcp)
{
	uint32_t ssrc;

	switch (msg->hdr.pt) {

	case RTCP_APP:
		ssrc = msg->r.app.src;
		break;

	case RTCP_SR:
		ssrc = msg->r.sr.ssrc;
		break;

	case RTCP_SDES:
		ssrc = msg->r.sdesv->src;
		break;

	case RTCP_PSFB:
		ssrc = msg->r.fb.ssrc_packet;
		break;

	case RTCP_BYE:
		ssrc = msg->r.bye.srcv[0];
		break;

	case RTCP_RR:
		ssrc = msg->r.rr.ssrc;
		break;

	default:
		warning("bundle: rtcp not sup (pt=%d)\n", msg->hdr.pt);
		return ENOTSUP;
	}

	*ssrcp = ssrc;

	return 0;
}


static struct stream *bundle_find_base(const struct list *streaml)
{
	struct le *le;

	for (le = list_head(streaml); le; le = le->next) {
		struct stream *strm = le->data;
		struct bundle *bun = stream_bundle(strm);

		if (bun && bun->state == BUNDLE_BASE)
			return strm;
	}

	return NULL;
}


/* send: used by multiplexed streams */
static bool udp_helper_send_handler(int *err, struct sa *dst,
				    struct mbuf *mb, void *arg)
{
	const struct list *streaml = arg;
	struct stream *strm;

#if 0
	if (bun->state != BUNDLE_MUX) {
		warning("send: expect state=mux, but state=%s\n",
			bundle_state_name(bun->state));
		return false;
	}
#endif

	strm = bundle_find_base(streaml);
	if (strm) {
		struct udp_sock *us = rtp_sock(stream_rtp_sock(strm));
		struct bundle *bun2 = stream_bundle(strm);
		int lerr;

		lerr = udp_send_helper(us, dst, mb, bun2->uh);
		if (lerr) {
			warning("bundle: send: %m\n", lerr);
			*err = lerr;
		}

		return true;  /* handled */
	}

	return false; /* continue */
}


/* recv: used by base stream */
static bool udp_helper_recv_handler(struct sa *src, struct mbuf *mb, void *arg)
{
	const struct list *streaml = arg;
	struct stream *strm;
	size_t pos = mb->pos;
	uint32_t ssrc;
	int err;

#if 0
	if (bun->state != BUNDLE_BASE) {
		warning("recv: expect state=base, but state=%s\n",
			bundle_state_name(bun->state));
		return false;
	}
#endif

	if (!rtp_is_rtcp_packet(mb)) {

		struct rtp_header hdr;

		err = rtp_hdr_decode(&hdr, mb);
		if (err) {
			warning("bundle: rtp decode error (%m)\n", err);
			return false;
		}

		ssrc = hdr.ssrc;
	}
	else {
		struct rtcp_msg *msg;

		err = rtcp_decode(&msg, mb);
		if (err) {
			warning("rtcp decode error (%m)\n", err);
			return false;
		}

		err = get_rtcp_ssrc(msg, &ssrc);
		mem_deref(msg);

		if (err)
			return false;
	}

	strm = lookup_remote_ssrc(streaml, ssrc);
	if (strm) {
		struct udp_sock *us = rtp_sock(stream_rtp_sock(strm));
		struct bundle *bun2 = stream_bundle(strm);

		mb->pos = pos;

		udp_recv_helper(us, src, mb, bun2->uh);
	}
	else {
		warning("bundle: stream not found (ssrc=%x)\n",
			ssrc);
	}

	return true; /* stop */
}


int bundle_start_socket(struct bundle *bun, struct udp_sock *us,
			struct list *streaml)
{
	enum { RTP_TRANSP_LAYER = 40 };
	bool muxed;
	bool based;
	int err;

	info("bundle: start socket <%p>\n", us);

	if (!bun || !us)
		return EINVAL;

	if (bun->uh)
		return EALREADY;

	muxed = bun->state == BUNDLE_MUX;
	based = bun->state == BUNDLE_BASE;

	/* NOTE: UDP helper must be injected below the RTP stack */
	err = udp_register_helper(&bun->uh, us, RTP_TRANSP_LAYER,
				  muxed ? udp_helper_send_handler : NULL,
				  based ? udp_helper_recv_handler : NULL,
				  streaml);
	if (err)
		return err;

	return 0;
}


enum bundle_state bundle_state(const struct bundle *bun)
{
	return bun ? bun->state : BUNDLE_NONE;
}


uint8_t bundle_extmap_mid(const struct bundle *bun)
{
	return bun ? bun->extmap_mid : 0;
}


int bundle_debug(struct re_printf *pf, const struct bundle *bun)
{
	int err = 0;

	if (!bun)
		return 0;

	err |= re_hprintf(pf, "*Bundle:\n");
	err |= re_hprintf(pf, " state:         %s\n",
			  bundle_state_name(bun->state));
	err |= re_hprintf(pf, " extmap_mid:    %u\n", bun->extmap_mid);
	err |= re_hprintf(pf, "\n");

	return err;
}
