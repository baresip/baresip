/**
 * @file stream.c  Generic Media Stream
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <string.h>
#include <time.h>
#include <re.h>
#include <baresip.h>
#include "core.h"


/** Magic number */
#define MAGIC 0x00511ea3
#include "magic.h"


enum {
	RTP_RECV_SIZE = 8192,
	RTP_CHECK_INTERVAL = 1000,  /* how often to check for RTP [ms] */
	PORT_DISCARD = 9,
};


static void print_rtp_stats(const struct stream *s)
{
	bool started = s->metric_tx.n_packets>0 || s->metric_rx.n_packets>0;

	if (!started)
		return;

	info("\n%-9s       Transmit:     Receive:\n"
	     "packets:        %7u      %7u\n"
	     "avg. bitrate:   %7.1f      %7.1f  (kbit/s)\n"
	     "errors:         %7d      %7d\n"
	     ,
	     sdp_media_name(s->sdp),
	     s->metric_tx.n_packets, s->metric_rx.n_packets,
	     1.0*metric_avg_bitrate(&s->metric_tx)/1000.0,
	     1.0*metric_avg_bitrate(&s->metric_rx)/1000.0,
	     s->metric_tx.n_err, s->metric_rx.n_err
	     );

	if (s->rtcp_stats.tx.sent || s->rtcp_stats.rx.sent) {

		info("pkt.report:     %7u      %7u\n"
		     "lost:           %7d      %7d\n"
		     "jitter:         %7.1f      %7.1f  (ms)\n",
		     s->rtcp_stats.tx.sent, s->rtcp_stats.rx.sent,
		     s->rtcp_stats.tx.lost, s->rtcp_stats.rx.lost,
		     1.0*s->rtcp_stats.tx.jit/1000,
		     1.0*s->rtcp_stats.rx.jit/1000);
	}
}


static void stream_destructor(void *arg)
{
	struct stream *s = arg;

	if (s->cfg.rtp_stats)
		print_rtp_stats(s);

	metric_reset(&s->metric_tx);
	metric_reset(&s->metric_rx);

	tmr_cancel(&s->tmr_rtp);
	list_unlink(&s->le);
	mem_deref(s->sdp);
	mem_deref(s->mes);
	mem_deref(s->mencs);
	mem_deref(s->mns);
	mem_deref(s->jbuf);
	mem_deref(s->rtp);
	mem_deref(s->cname);
}


static bool mnat_ready(const struct stream *strm)
{
	if (strm->mnat && strm->mnat->wait_connected)
		return strm->mnat_connected;
	else
		return true;
}


static void stream_close(struct stream *strm, int err)
{
	stream_error_h *errorh = strm->errorh;

	strm->terminated = true;
	strm->errorh = NULL;

	if (errorh)
		errorh(strm, err, strm->sess_arg);
}


static void check_rtp_handler(void *arg)
{
	struct stream *strm = arg;
	const uint64_t now = tmr_jiffies();
	int diff_ms;

	MAGIC_CHECK(strm);

	tmr_start(&strm->tmr_rtp, RTP_CHECK_INTERVAL,
		  check_rtp_handler, strm);

	/* If no RTP was received at all, check later */
	if (!strm->ts_last)
		return;

	/* We are in sendrecv mode, check when the last RTP packet
	 * was received.
	 */
	if (sdp_media_dir(strm->sdp) == SDP_SENDRECV) {

		diff_ms = (int)(now - strm->ts_last);

		debug("stream: last \"%s\" RTP packet: %d milliseconds\n",
		      sdp_media_name(strm->sdp), diff_ms);

		/* check for large jumps in time */
		if (diff_ms > (3600 * 1000)) {
			strm->ts_last = 0;
			return;
		}

		if (diff_ms > (int)strm->rtp_timeout_ms) {

			info("stream: no %s RTP packets received for"
			     " %d milliseconds\n",
			     sdp_media_name(strm->sdp), diff_ms);

			stream_close(strm, ETIMEDOUT);
		}
	}
	else {
		re_printf("check_rtp: not checking (dir=%s)\n",
			  sdp_dir_name(sdp_media_dir(strm->sdp)));
	}
}


static inline int lostcalc(struct stream *s, uint16_t seq)
{
	const uint16_t delta = seq - s->pseq;
	int lostc;

	if (s->pseq == (uint32_t)-1)
		lostc = 0;
	else if (delta == 0)
		return -1;
	else if (delta < 3000)
		lostc = delta - 1;
	else if (delta < 0xff9c)
		lostc = 0;
	else
		return -2;

	s->pseq = seq;

	return lostc;
}


static const char *media_name(enum media_type type)
{
	switch (type) {

	case MEDIA_AUDIO: return "audio";
	case MEDIA_VIDEO: return "video";
	default:          return "???";
	}
}


static void handle_rtp(struct stream *s, const struct rtp_header *hdr,
		       struct mbuf *mb, unsigned lostc)
{
	struct rtpext extv[8];
	size_t extc = 0;

	/* RFC 5285 -- A General Mechanism for RTP Header Extensions */
	if (hdr->ext && hdr->x.len && mb) {

		const size_t pos = mb->pos;
		const size_t end = mb->end;
		const size_t ext_stop = mb->pos;
		size_t ext_len;
		size_t i;
		int err;

		if (hdr->x.type != RTPEXT_TYPE_MAGIC) {
			debug("stream: unknown ext type ignored (0x%04x)\n",
			     hdr->x.type);
			goto handler;
		}

		ext_len = hdr->x.len*sizeof(uint32_t);
		if (mb->pos < ext_len) {
			warning("stream: corrupt rtp packet,"
				" not enough space for rtpext of %zu bytes\n",
				ext_len);
			return;
		}

		mb->pos = mb->pos - ext_len;
		mb->end = ext_stop;

		for (i=0; i<ARRAY_SIZE(extv) && mbuf_get_left(mb); i++) {

			err = rtpext_decode(&extv[i], mb);
			if (err) {
				warning("stream: rtpext_decode failed (%m)\n",
					err);
				return;
			}
		}

		extc = i;

		mb->pos = pos;
		mb->end = end;
	}

 handler:
	s->rtph(hdr, extv, extc, mb, lostc, s->arg);
}


static inline bool is_rtcp_packet(unsigned pt)
{
	return 64 <= pt && pt <= 95;
}


static void rtp_handler(const struct sa *src, const struct rtp_header *hdr,
			struct mbuf *mb, void *arg)
{
	struct stream *s = arg;
	bool flush = false;
	int err;

	MAGIC_CHECK(s);

	if (is_rtcp_packet(hdr->pt)) {
		info("stream: drop incoming RTCP packet on RTP port"
		     " (pt=%u)\n", hdr->pt);
		return;
	}

	s->ts_last = tmr_jiffies();

	if (!mbuf_get_left(mb))
		return;

	if (!(sdp_media_ldir(s->sdp) & SDP_RECVONLY))
		return;

#if 0
	/* The marker bit indicates the beginning of a talkspurt. */
	if (hdr->m && s->type == MEDIA_AUDIO)
		flush = true;
#endif

	metric_add_packet(&s->metric_rx, mbuf_get_left(mb));

	if (!s->rtp_estab) {
		info("stream: incoming rtp for '%s' established"
		     ", receiving from %J\n",
		     sdp_media_name(s->sdp), src);
		s->rtp_estab = true;

		if (s->rtpestabh)
			s->rtpestabh(s, s->sess_arg);
	}

	if (!s->pseq_set) {
		s->ssrc_rx = hdr->ssrc;
		s->pseq = hdr->seq - 1;
		s->pseq_set = true;
	}
	else if (hdr->ssrc != s->ssrc_rx) {

		info("stream: %s: SSRC changed 0x%x -> 0x%x"
		     " (%u bytes from %J)\n",
		     sdp_media_name(s->sdp), s->ssrc_rx, hdr->ssrc,
		     mbuf_get_left(mb), src);

		s->ssrc_rx = hdr->ssrc;
		s->pseq = hdr->seq - 1;
		flush = true;
	}

	if (s->jbuf) {

		struct rtp_header hdr2;
		void *mb2 = NULL;
		int lostc;

		/* Put frame in Jitter Buffer */
		if (flush && s->jbuf_started)
			jbuf_flush(s->jbuf);

		err = jbuf_put(s->jbuf, hdr, mb);
		if (err) {
			info("stream: %s: dropping %u bytes from %J"
			     " [seq=%u, ts=%u] (%m)\n",
			     sdp_media_name(s->sdp), mb->end,
			     src, hdr->seq, hdr->ts, err);
			s->metric_rx.n_err++;
		}

		if (jbuf_get(s->jbuf, &hdr2, &mb2)) {

			if (!s->jbuf_started)
				return;

			memset(&hdr2, 0, sizeof(hdr2));
		}

		s->jbuf_started = true;

		lostc = lostcalc(s, hdr2.seq);

		handle_rtp(s, &hdr2, mb2, lostc > 0 ? lostc : 0);

		mem_deref(mb2);
	}
	else {
		handle_rtp(s, hdr, mb, 0);
	}
}


static void rtcp_handler(const struct sa *src, struct rtcp_msg *msg, void *arg)
{
	struct stream *s = arg;
	(void)src;

	MAGIC_CHECK(s);

	s->ts_last = tmr_jiffies();

	switch (msg->hdr.pt) {

	case RTCP_SR:
		(void)rtcp_stats(s->rtp, msg->r.sr.ssrc, &s->rtcp_stats);
		break;
	}

	if (s->rtcph)
		s->rtcph(s, msg, s->arg);

	if (s->sessrtcph)
		s->sessrtcph(s, msg, s->sess_arg);
}


static int stream_sock_alloc(struct stream *s, int af)
{
	struct sa laddr;
	int tos, err;

	if (!s)
		return EINVAL;

	/* we listen on all interfaces */
	sa_init(&laddr, af);

	err = rtp_listen(&s->rtp, IPPROTO_UDP, &laddr,
			 s->cfg.rtp_ports.min, s->cfg.rtp_ports.max,
			 true, rtp_handler, rtcp_handler, s);
	if (err) {
		warning("stream: rtp_listen failed: af=%s ports=%u-%u"
			" (%m)\n", net_af2name(af),
			s->cfg.rtp_ports.min, s->cfg.rtp_ports.max, err);
		return err;
	}

	tos = s->cfg.rtp_tos;
	(void)udp_setsockopt(rtp_sock(s->rtp), IPPROTO_IP, IP_TOS,
			     &tos, sizeof(tos));
	(void)udp_setsockopt(rtcp_sock(s->rtp), IPPROTO_IP, IP_TOS,
			     &tos, sizeof(tos));

	udp_rxsz_set(rtp_sock(s->rtp), RTP_RECV_SIZE);

	udp_sockbuf_set(rtp_sock(s->rtp), 65536);

	return 0;
}


/**
 * Start media encryption
 *
 * @param strm   Stream object
 *
 * @return 0 if success, otherwise errorcode
 */
int stream_start_mediaenc(struct stream *strm)
{
	int err;

	if (!strm)
		return EINVAL;

	if (strm->menc && strm->menc->mediah) {

		info("stream: %s: starting mediaenc '%s' (wait_secure=%d)\n",
		     media_name(strm->type), strm->menc->id,
		     strm->menc->wait_secure);

		err = strm->menc->mediah(&strm->mes, strm->mencs, strm->rtp,
				 rtp_sock(strm->rtp),
				 strm->rtcp_mux ? NULL : rtcp_sock(strm->rtp),
				 &strm->raddr_rtp,
				 strm->rtcp_mux ? NULL : &strm->raddr_rtcp,
					 strm->sdp, strm);
		if (err) {
			warning("stream: start mediaenc error: %m\n", err);
			return err;
		}
	}

	return 0;
}


static void mnat_connected_handler(const struct sa *raddr1,
				   const struct sa *raddr2, void *arg)
{
	struct stream *strm = arg;

	info("stream: mnat '%s' connected: raddr %J %J\n",
	     strm->mnat->id, raddr1, raddr2);

	strm->raddr_rtp = *raddr1;

	if (strm->rtcp_mux)
		strm->raddr_rtcp = *raddr1;
	else if (raddr2)
		strm->raddr_rtcp = *raddr2;

	strm->mnat_connected = true;

	if (stream_is_ready(strm)) {

		stream_start(strm);
	}

	if (strm->mnatconnh)
		strm->mnatconnh(strm, strm->sess_arg);
}


int stream_alloc(struct stream **sp, struct list *streaml,
		 const struct stream_param *prm,
		 const struct config_avt *cfg,
		 struct sdp_session *sdp_sess,
		 enum media_type type, int label,
		 const struct mnat *mnat, struct mnat_sess *mnat_sess,
		 const struct menc *menc, struct menc_sess *menc_sess,
		 bool offerer,
		 stream_rtp_h *rtph, stream_rtcp_h *rtcph, void *arg)
{
	struct stream *s;
	int err;

	if (!sp || !prm || !cfg || !rtph)
		return EINVAL;

	s = mem_zalloc(sizeof(*s), stream_destructor);
	if (!s)
		return ENOMEM;

	MAGIC_INIT(s);

	s->cfg   = *cfg;
	s->type  = type;
	s->rtph  = rtph;
	s->rtcph = rtcph;
	s->arg   = arg;
	s->pseq  = -1;

	if (prm->use_rtp) {
		err = stream_sock_alloc(s, prm->af);
		if (err) {
			warning("stream: failed to create socket"
				" for media '%s' (%m)\n",
				media_name(type), err);
			goto out;
		}
	}

	err = str_dup(&s->cname, prm->cname);
	if (err)
		goto out;

	/* Jitter buffer */
	if (cfg->jbuf_del.min && cfg->jbuf_del.max) {

		err = jbuf_alloc(&s->jbuf, cfg->jbuf_del.min,
				 cfg->jbuf_del.max);
		if (err)
			goto out;
	}

	err = sdp_media_add(&s->sdp, sdp_sess, media_name(type),
			    s->rtp ? sa_port(rtp_local(s->rtp)) : PORT_DISCARD,
			    (menc && menc->sdp_proto) ? menc->sdp_proto :
			    sdp_proto_rtpavp);
	if (err)
		goto out;

	if (label) {
		err |= sdp_media_set_lattr(s->sdp, true,
					   "label", "%d", label);
	}

	/* RFC 5506 */
	if (offerer || sdp_media_rattr(s->sdp, "rtcp-rsize"))
		err |= sdp_media_set_lattr(s->sdp, true, "rtcp-rsize", NULL);

	/* RFC 5576 */
	err |= sdp_media_set_lattr(s->sdp, true,
				   "ssrc", "%u cname:%s",
				   rtp_sess_ssrc(s->rtp), prm->cname);

	/* RFC 5761 */
	if (cfg->rtcp_mux &&
	    (offerer || sdp_media_rattr(s->sdp, "rtcp-mux"))) {

		err |= sdp_media_set_lattr(s->sdp, true, "rtcp-mux", NULL);
	}

	if (err)
		goto out;

	if (mnat && s->rtp) {
		s->mnat = mnat;
		err = mnat->mediah(&s->mns, mnat_sess,
				   rtp_sock(s->rtp),
				   cfg->rtcp_mux ? NULL : rtcp_sock(s->rtp),
				   s->sdp, mnat_connected_handler, s);
		if (err)
			goto out;
	}

	if (menc && s->rtp) {
		s->menc  = menc;
		s->mencs = mem_ref(menc_sess);

		err = stream_start_mediaenc(s);
		if (err)
			goto out;
	}

	s->pt_enc = -1;

	err  = metric_init(&s->metric_tx);
	err |= metric_init(&s->metric_rx);
	if (err)
		goto out;

	list_append(streaml, &s->le, s);

 out:
	if (err)
		mem_deref(s);
	else
		*sp = s;

	return err;
}


/**
 * Get the sdp object from the stream
 *
 * @param strm Stream object
 *
 * @return SDP media object
 */
struct sdp_media *stream_sdpmedia(const struct stream *strm)
{
	return strm ? strm->sdp : NULL;
}


int stream_send(struct stream *s, bool ext, bool marker, int pt, uint32_t ts,
		struct mbuf *mb)
{
	int err = 0;

	if (!s)
		return EINVAL;

	if (!sa_isset(&s->raddr_rtp, SA_ALL))
		return 0;
	if (!(sdp_media_rdir(s->sdp) & SDP_SENDONLY))
		return 0;
	if (s->hold)
		return 0;

	if (!stream_is_ready(s)) {
		warning("stream: send: not ready\n");
		return EINTR;
	}

	metric_add_packet(&s->metric_tx, mbuf_get_left(mb));

	if (pt < 0)
		pt = s->pt_enc;

	if (pt >= 0) {
		err = rtp_send(s->rtp, &s->raddr_rtp, ext,
			       marker, pt, ts, mb);
		if (err)
			s->metric_tx.n_err++;
	}

	return err;
}


static void stream_remote_set(struct stream *s)
{
	if (!s)
		return;

	/* RFC 5761 */
	if (s->cfg.rtcp_mux && sdp_media_rattr(s->sdp, "rtcp-mux")) {

		if (!s->rtcp_mux)
			info("%s: RTP/RTCP multiplexing enabled\n",
			     sdp_media_name(s->sdp));
		s->rtcp_mux = true;

		sdp_media_set_lattr(s->sdp, true, "rtcp-mux", NULL);
	}

	rtcp_enable_mux(s->rtp, s->rtcp_mux);

	sa_cpy(&s->raddr_rtp, sdp_media_raddr(s->sdp));

	if (s->rtcp_mux)
		s->raddr_rtcp = s->raddr_rtp;
	else
		sdp_media_raddr_rtcp(s->sdp, &s->raddr_rtcp);

	if (stream_is_ready(s)) {

		stream_start(s);
	}
}


/**
 * Update the media stream
 *
 * @param s Stream object
 */
void stream_update(struct stream *s)
{
	const struct sdp_format *fmt;
	int err = 0;

	if (!s)
		return;

	info("stream: update '%s'\n", media_name(s->type));

	fmt = sdp_media_rformat(s->sdp, NULL);

	s->pt_enc = fmt ? fmt->pt : -1;

	if (sdp_media_has_media(s->sdp))
		stream_remote_set(s);

	if (s->mencs && mnat_ready(s)) {

		err = stream_start_mediaenc(s);
		if (err) {
			warning("stream: mediaenc update: %m\n", err);
		}
	}
}


void stream_update_encoder(struct stream *s, int pt_enc)
{
	if (!s)
		return;

	if (pt_enc >= 0)
		s->pt_enc = pt_enc;
}


int stream_jbuf_stat(struct re_printf *pf, const struct stream *s)
{
	struct jbuf_stat stat;
	int err;

	if (!s)
		return EINVAL;

	err  = re_hprintf(pf, " %s:", sdp_media_name(s->sdp));

	err |= jbuf_stats(s->jbuf, &stat);
	if (err) {
		err = re_hprintf(pf, "Jbuf stat: (not available)");
	}
	else {
		err = re_hprintf(pf, "Jbuf stat: put=%u get=%u or=%u ur=%u",
				  stat.n_put, stat.n_get,
				  stat.n_overflow, stat.n_underflow);
	}

	return err;
}


void stream_hold(struct stream *s, bool hold)
{
	if (!s)
		return;

	s->hold = hold;
	sdp_media_set_ldir(s->sdp, hold ? SDP_SENDONLY : SDP_SENDRECV);
	stream_reset(s);
}


void stream_set_srate(struct stream *s, uint32_t srate_tx, uint32_t srate_rx)
{
	if (!s)
		return;

	if (srate_tx)
		rtcp_set_srate_tx(s->rtp, srate_tx);
	if (srate_rx)
		rtcp_set_srate_rx(s->rtp, srate_rx);
}


void stream_send_fir(struct stream *s, bool pli)
{
	int err;

	if (!s)
		return;

	if (pli)
		err = rtcp_send_pli(s->rtp, s->ssrc_rx);
	else
		err = rtcp_send_fir(s->rtp, rtp_sess_ssrc(s->rtp));

	if (err) {
		s->metric_tx.n_err++;

		warning("stream: failed to send RTCP %s: %m\n",
			pli ? "PLI" : "FIR", err);
	}
}


void stream_reset(struct stream *s)
{
	if (!s)
		return;

	if (s->jbuf && s->jbuf_started)
		jbuf_flush(s->jbuf);
}


void stream_set_bw(struct stream *s, uint32_t bps)
{
	if (!s)
		return;

	sdp_media_set_lbandwidth(s->sdp, SDP_BANDWIDTH_AS, bps / 1000);
}


void stream_enable_rtp_timeout(struct stream *strm, uint32_t timeout_ms)
{
	if (!strm)
		return;

	strm->rtp_timeout_ms = timeout_ms;

	tmr_cancel(&strm->tmr_rtp);

	if (timeout_ms) {

		info("stream: Enable RTP timeout (%u milliseconds)\n",
		     timeout_ms);

		strm->ts_last = tmr_jiffies();
		tmr_start(&strm->tmr_rtp, 10, check_rtp_handler, strm);
	}
}


/**
 * Set optional session handlers
 *
 * @param strm      Stream object
 * @param mnatconnh Media NAT connected handler
 * @param rtpestabh Incoming RTP established handler
 * @param rtcph     Incoming RTCP message handler
 * @param errorh    Error handler
 * @param arg       Handler argument
 */
void stream_set_session_handlers(struct stream *strm,
				 stream_mnatconn_h *mnatconnh,
				 stream_rtpestab_h *rtpestabh,
				 stream_rtcp_h *rtcph,
				 stream_error_h *errorh, void *arg)
{
	if (!strm)
		return;

	strm->mnatconnh  = mnatconnh;
	strm->rtpestabh  = rtpestabh;
	strm->sessrtcph  = rtcph;
	strm->errorh     = errorh;
	strm->sess_arg   = arg;
}


/**
 * Print stream debug info
 *
 * @param pf Print function
 * @param s  Stream object
 *
 * @return 0 if success, otherwise errorcode
 */
int stream_debug(struct re_printf *pf, const struct stream *s)
{
	int err;

	if (!s)
		return 0;

	err  = re_hprintf(pf, " %s dir=%s pt_enc=%d\n", sdp_media_name(s->sdp),
			  sdp_dir_name(sdp_media_dir(s->sdp)),
			  s->pt_enc);

	err |= re_hprintf(pf, " local: %J, remote: %J/%J\n",
			  sdp_media_laddr(s->sdp),
			  &s->raddr_rtp, &s->raddr_rtcp);

	err |= re_hprintf(pf, " mnat: %s (connected=%s)\n",
			  s->mnat ? s->mnat->id : "(none)",
			  s->mnat_connected ? "yes" : "no");

	err |= re_hprintf(pf, " menc: %s (secure=%s)\n",
			  s->menc ? s->menc->id : "(none)",
			  s->menc_secure ? "yes" : "no");

	err |= rtp_debug(pf, s->rtp);
	err |= jbuf_debug(pf, s->jbuf);

	return err;
}


int stream_print(struct re_printf *pf, const struct stream *s)
{
	if (!s)
		return 0;

	return re_hprintf(pf, " %s=%u/%u", sdp_media_name(s->sdp),
			  s->metric_tx.cur_bitrate,
			  s->metric_rx.cur_bitrate);
}


/**
 * Get the RTCP Statistics from a media stream
 *
 * @param strm Stream object
 *
 * @return RTCP Statistics
 */
const struct rtcp_stats *stream_rtcp_stats(const struct stream *strm)
{
	return strm ? &strm->rtcp_stats : NULL;
}


/**
 * Get the number of transmitted RTP packets
 *
 * @param strm Stream object
 *
 * @return Number of transmitted RTP packets
 */
uint32_t stream_metric_get_tx_n_packets(const struct stream *strm)
{
	return strm ? strm->metric_tx.n_packets : 0;
}


/**
 * Get the number of transmitted RTP bytes
 *
 * @param strm Stream object
 *
 * @return Number of transmitted RTP bytes
 */
uint32_t stream_metric_get_tx_n_bytes(const struct stream *strm)
{
	return strm ? strm->metric_tx.n_bytes : 0;
}


/**
 * Get the number of transmission errors
 *
 * @param strm Stream object
 *
 * @return Number of transmission errors
 */
uint32_t stream_metric_get_tx_n_err(const struct stream *strm)
{
	return strm ? strm->metric_tx.n_err : 0;
}


/**
 * Get the number of received RTP packets
 *
 * @param strm Stream object
 *
 * @return Number of received RTP packets
 */
uint32_t stream_metric_get_rx_n_packets(const struct stream *strm)
{
	return strm ? strm->metric_rx.n_packets : 0;
}


/**
 * Get the number of received RTP bytes
 *
 * @param strm Stream object
 *
 * @return Number of received RTP bytes
 */
uint32_t stream_metric_get_rx_n_bytes(const struct stream *strm)
{
	return strm ? strm->metric_rx.n_bytes : 0;
}


/**
 * Get the number of receive errors
 *
 * @param strm Stream object
 *
 * @return Number of receive errors
 */
uint32_t stream_metric_get_rx_n_err(const struct stream *strm)
{
	return strm ? strm->metric_rx.n_err : 0;
}


int stream_jbuf_reset(struct stream *strm,
		      uint32_t frames_min, uint32_t frames_max)
{
	if (!strm)
		return EINVAL;

	strm->jbuf = mem_deref(strm->jbuf);

	if (frames_min && frames_max)
		return jbuf_alloc(&strm->jbuf, frames_min, frames_max);

	return 0;
}


bool stream_is_ready(const struct stream *strm)
{
	if (!strm)
		return false;

	/* Media NAT */
	if (strm->mnat) {
		if (!mnat_ready(strm))
			return false;
	}

	/* Media Enc */
	if (strm->menc && strm->menc->wait_secure) {

		if (!strm->menc_secure)
			return false;
	}

	if (!sa_isset(&strm->raddr_rtp, SA_ALL))
		return false;

	return !strm->terminated;
}


/**
 * Set the secure flag on the stream object
 *
 * @param strm   Stream object
 * @param secure True for secure, false for insecure
 */
void stream_set_secure(struct stream *strm, bool secure)
{
	if (!strm)
		return;

	strm->menc_secure = secure;
}


/**
 * Get the secure flag on the stream object
 *
 * @param strm   Stream object
 *
 * @return True for secure, false for insecure
 */
bool stream_is_secure(const struct stream *strm)
{
	return strm ? strm->menc_secure : false;
}


/**
 * Start the media stream
 *
 * @param strm   Stream object
 *
 * @return 0 if success, otherwise errorcode
 */
int stream_start(const struct stream *strm)
{
	int err;

	if (!strm)
		return EINVAL;

	debug("stream: %s: starting RTCP with remote %J\n",
	      media_name(strm->type), &strm->raddr_rtcp);

	rtcp_start(strm->rtp, strm->cname, &strm->raddr_rtcp);

	if (!strm->mnat) {
		/* Send a dummy RTCP packet to open NAT pinhole */
		err = rtcp_send_app(strm->rtp, "PING", (void *)"PONG", 4);
		if (err) {
			warning("stream: rtcp_send_app failed (%m)\n", err);
			return err;
		}
	}

	return 0;
}


/**
 * Get the name of the stream type (e.g. audio or video)
 *
 * @param strm Stream object
 *
 * @return Name of stream type
 */
const char *stream_name(const struct stream *strm)
{
	if (!strm)
		return NULL;

	return media_name(strm->type);
}
