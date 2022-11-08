/**
 * @file stream.c  Generic Media Stream
 *
 * Copyright (C) 2010 Alfred E. Heggestad
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


/* Transmit */
struct sender {
	struct metric *metric; /**< Metrics for transmit            */
	struct sa raddr_rtp;   /**< Remote RTP address              */
	struct sa raddr_rtcp;  /**< Remote RTCP address             */
	int pt_enc;            /**< Payload type for encoding       */
};


/* Receive */
struct receiver {
	struct metric *metric; /**< Metrics for receiving            */
	struct tmr tmr_rtp;   /**< Timer for detecting RTP timeout  */
	struct jbuf *jbuf;    /**< Jitter Buffer for incoming RTP   */
	uint64_t ts_last;     /**< Timestamp of last recv RTP pkt   */
	uint32_t rtp_timeout; /**< RTP Timeout value in [ms]        */
	uint32_t ssrc;        /**< Incoming synchronization source  */
	uint32_t pseq;        /**< Sequence number for incoming RTP */
	bool ssrc_set;        /**< Incoming SSRC is set             */
	bool pseq_set;        /**< True if sequence number is set   */
	bool rtp_estab;       /**< True if RTP stream established   */
	bool enabled;         /**< True if enabled                  */
};


/** Defines a generic media stream */
struct stream {
#ifndef RELEASE
	uint32_t magic;          /**< Magic number for debugging            */
#endif
	struct le le;            /**< Linked list element                   */
	struct config_avt cfg;   /**< Stream configuration                  */
	struct sdp_media *sdp;   /**< SDP Media line                        */
	enum sdp_dir ldir;       /**< SDP direction of the stream           */
	struct rtp_sock *rtp;    /**< RTP Socket                            */
	struct rtcp_stats rtcp_stats;/**< RTCP statistics                   */
	const struct mnat *mnat; /**< Media NAT traversal module            */
	struct mnat_media *mns;  /**< Media NAT traversal state             */
	const struct menc *menc; /**< Media encryption module               */
	struct menc_sess *mencs; /**< Media encryption session state        */
	struct menc_media *mes;  /**< Media Encryption media state          */
	enum media_type type;    /**< Media type, e.g. audio/video          */
	char *cname;             /**< RTCP Canonical end-point identifier   */
	char *peer;              /**< Peer URI/name or identifier           */
	char *mid;               /**< Media stream identification           */
	bool rtcp_mux;           /**< RTP/RTCP multiplex supported by peer  */
	bool terminated;         /**< Stream is terminated flag             */
	bool hold;               /**< Stream is on-hold (local)             */
	bool mnat_connected;     /**< Media NAT is connected                */
	bool menc_secure;        /**< Media stream is secure                */
	stream_pt_h *pth;        /**< Stream payload type handler           */
	stream_rtp_h *rtph;      /**< Stream RTP handler                    */
	stream_rtcp_h *rtcph;    /**< Stream RTCP handler                   */
	void *arg;               /**< Handler argument                      */
	stream_mnatconn_h *mnatconnh;/**< Medianat connected handler        */
	stream_rtpestab_h *rtpestabh;/**< RTP established handler           */
	stream_rtcp_h *sessrtcph;    /**< Stream RTCP handler               */
	stream_error_h *errorh;  /**< Stream error handler                  */
	void *sess_arg;          /**< Session handlers argument             */

	struct bundle *bundle;
	uint8_t extmap_counter;

	struct sender tx;

	struct receiver rx;
};


static void print_rtp_stats(const struct stream *s)
{
	uint32_t tx_n_packets = metric_n_packets(s->tx.metric);
	uint32_t rx_n_packets = metric_n_packets(s->rx.metric);
	bool started = tx_n_packets > 0 || rx_n_packets > 0;

	if (!started)
		return;

	info("\n%-9s       Transmit:     Receive:\n"
	     "packets:        %7u      %7u\n"
	     "avg. bitrate:   %7.1f      %7.1f  (kbit/s)\n"
	     "errors:         %7d      %7d\n"
	     ,
	     sdp_media_name(s->sdp),
	     tx_n_packets, rx_n_packets,
	     1.0*metric_avg_bitrate(s->tx.metric)/1000.0,
	     1.0*metric_avg_bitrate(s->rx.metric)/1000.0,
	     metric_n_err(s->tx.metric),
	     metric_n_err(s->rx.metric)
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

	mem_deref(s->tx.metric);
	mem_deref(s->rx.metric);

	tmr_cancel(&s->rx.tmr_rtp);
	list_unlink(&s->le);
	mem_deref(s->sdp);
	mem_deref(s->mes);
	mem_deref(s->mencs);
	mem_deref(s->mns);
	mem_deref(s->rx.jbuf);
	mem_deref(s->bundle);  /* NOTE: deref before rtp */
	mem_deref(s->rtp);
	mem_deref(s->cname);
	mem_deref(s->peer);
	mem_deref(s->mid);
}


static const char *media_name(enum media_type type)
{
	switch (type) {

	case MEDIA_AUDIO: return "audio";
	case MEDIA_VIDEO: return "video";
	default:          return "???";
	}
}


static void send_set_raddr(struct stream *strm, const struct sa *raddr)
{
	debug("stream: set remote addr for '%s': %J\n",
	     media_name(strm->type), raddr);

	strm->tx.raddr_rtp  = *raddr;
	strm->tx.raddr_rtcp = *raddr;
}


static void recv_set_ssrc(struct receiver *rx, uint32_t ssrc)
{
	if (rx->ssrc_set) {
		if (ssrc != rx->ssrc)
			info("stream: receive: SSRC changed: %x -> %x\n",
			     rx->ssrc, ssrc);
		rx->ssrc = ssrc;
	}
	else {
		info("stream: receive: setting SSRC: %x\n", ssrc);
		rx->ssrc = ssrc;
		rx->ssrc_set = true;
	}
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
	jbuf_flush(strm->rx.jbuf);

	if (errorh)
		errorh(strm, err, strm->sess_arg);
}


static void check_rtp_handler(void *arg)
{
	struct stream *strm = arg;
	const uint64_t now = tmr_jiffies();
	int diff_ms;

	MAGIC_CHECK(strm);

	tmr_start(&strm->rx.tmr_rtp, RTP_CHECK_INTERVAL,
		  check_rtp_handler, strm);

	/* If no RTP was received at all, check later */
	if (!strm->rx.ts_last)
		return;

	/* We are in sendrecv mode, check when the last RTP packet
	 * was received.
	 */
	if (sdp_media_dir(strm->sdp) == SDP_SENDRECV) {

		diff_ms = (int)(now - strm->rx.ts_last);

		debug("stream: last \"%s\" RTP packet: %d milliseconds\n",
		      sdp_media_name(strm->sdp), diff_ms);

		/* check for large jumps in time */
		if (diff_ms > (3600 * 1000)) {
			strm->rx.ts_last = 0;
			return;
		}

		if (diff_ms > (int)strm->rx.rtp_timeout) {

			info("stream: no %s RTP packets received for"
			     " %d milliseconds\n",
			     sdp_media_name(strm->sdp), diff_ms);

			stream_close(strm, ETIMEDOUT);
		}
	}
	else {
		debug("check_rtp: not checking \"%s\" RTP (dir=%s)\n",
			  sdp_media_name(strm->sdp),
			  sdp_dir_name(sdp_media_dir(strm->sdp)));
	}
}


static inline int lostcalc(struct receiver *rx, uint16_t seq)
{
	const uint16_t delta = seq - rx->pseq;
	int lostc;

	if (rx->pseq == (uint32_t)-1)
		lostc = 0;
	else if (delta == 0)
		return -1;
	else if (delta < 3000)
		lostc = delta - 1;
	else if (delta < 0xff9c)
		lostc = 0;
	else
		return -2;

	rx->pseq = seq;

	return lostc;
}


static int handle_rtp(struct stream *s, const struct rtp_header *hdr,
		       struct mbuf *mb, unsigned lostc, bool drop)
{
	struct rtpext extv[8];
	size_t extc = 0;
	bool ignore = drop;

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
			return 0;
		}

		mb->pos = mb->pos - ext_len;
		mb->end = ext_stop;

		for (i=0; i<ARRAY_SIZE(extv) && mbuf_get_left(mb); i++) {

			err = rtpext_decode(&extv[i], mb);
			if (err) {
				warning("stream: rtpext_decode failed (%m)\n",
					err);
				return 0;
			}
		}

		extc = i;

		mb->pos = pos;
		mb->end = end;
	}

 handler:
	s->rtph(hdr, extv, extc, mb, lostc, &ignore, s->arg);
	if (ignore)
		return EAGAIN;

	return 0;
}


static void rtp_handler(const struct sa *src, const struct rtp_header *hdr,
			struct mbuf *mb, void *arg)
{
	struct stream *s = arg;
	bool flush = false;
	bool first = false;
	int err;

	MAGIC_CHECK(s);

	if (!s->rx.enabled && s->type == MEDIA_AUDIO)
		return;

	if (rtp_pt_is_rtcp(hdr->pt)) {
		info("stream: drop incoming RTCP packet on RTP port"
		     " (pt=%u)\n", hdr->pt);
		return;
	}

	s->rx.ts_last = tmr_jiffies();

	if (!(sdp_media_ldir(s->sdp) & SDP_RECVONLY))
		return;

	metric_add_packet(s->rx.metric, mbuf_get_left(mb));

	if (!s->rx.rtp_estab) {
		info("stream: incoming rtp for '%s' established"
		     ", receiving from %J\n",
		     sdp_media_name(s->sdp), src);
		s->rx.rtp_estab = true;

		if (s->rtpestabh)
			s->rtpestabh(s, s->sess_arg);
	}

	if (!s->rx.pseq_set) {
		s->rx.ssrc = hdr->ssrc;
		s->rx.ssrc_set = true;
		s->rx.pseq = hdr->seq - 1;
		s->rx.pseq_set = true;
		first = true;
	}
	else if (hdr->ssrc != s->rx.ssrc) {

		info("stream: %s: SSRC changed 0x%x -> 0x%x"
		     " (%u bytes from %J)\n",
		     sdp_media_name(s->sdp), s->rx.ssrc, hdr->ssrc,
		     mbuf_get_left(mb), src);

		s->rx.ssrc = hdr->ssrc;
		s->rx.pseq = hdr->seq - 1;
		flush = true;
	}

	/* payload-type changed? */
	err = s->pth(hdr->pt, mb, s->arg);
	if (err && err != ENODATA)
		return;

	if (s->rx.jbuf) {

		/* Put frame in Jitter Buffer */
		if (flush)
			jbuf_flush(s->rx.jbuf);

		if (first && err == ENODATA)
			return;

		err = jbuf_put(s->rx.jbuf, hdr, mb);
		if (err) {
			info("stream: %s: dropping %u bytes from %J"
			     " [seq=%u, ts=%u] (%m)\n",
			     sdp_media_name(s->sdp), mb->end,
			     src, hdr->seq, hdr->ts, err);
			metric_inc_err(s->rx.metric);
		}


		if (stream_decode(s) == EAGAIN)
			(void) stream_decode(s);
	}
	else {
		(void)handle_rtp(s, hdr, mb, 0, false);
	}
}


/**
 * Decodes one RTP packet. For audio streams this function is called by the
 * auplay write handler and runs in the auplay thread. For video streams there
 * is only the RTP thread which also does the decoding.
 *
 * @param s The stream
 *
 * @return 0 if success, EAGAIN if it should be called again in order to avoid
 * a jitter buffer overflow, otherwise errorcode
 */
int stream_decode(struct stream *s)
{
	struct rtp_header hdr;
	void *mb;
	int lostc;
	int err;
	int err2;

	if (!s)
		return EINVAL;

	if (!s->rx.jbuf)
		return ENOENT;

	err = jbuf_get(s->rx.jbuf, &hdr, &mb);
	if (err && err != EAGAIN)
		return ENOENT;

	lostc = lostcalc(&s->rx, hdr.seq);

	err2 = handle_rtp(s, &hdr, mb, lostc > 0 ? lostc : 0, err == EAGAIN);
	mem_deref(mb);

	if (err2 == EAGAIN)
		return err2;

	return err;
}


static void rtcp_handler(const struct sa *src, struct rtcp_msg *msg, void *arg)
{
	struct stream *s = arg;
	(void)src;

	MAGIC_CHECK(s);

	s->rx.ts_last = tmr_jiffies();

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

	tos = s->type == MEDIA_AUDIO ? s->cfg.rtp_tos : s->cfg.rtpv_tos;
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
				 &strm->tx.raddr_rtp,
				 strm->rtcp_mux ? NULL : &strm->tx.raddr_rtcp,
					 strm->sdp, strm);
		if (err) {
			warning("stream: start mediaenc error: %m\n", err);
			return err;
		}
	}

	return 0;
}


static void update_all_remote_addr(struct list *streaml,
				   const struct sa *raddr)
{
	struct le *le;

	for (le = streaml->head; le; le = le->next) {

		struct stream *strm = le->data;

		if (bundle_state(stream_bundle(strm)) == BUNDLE_MUX) {

			send_set_raddr(strm, raddr);
		}
	}
}


static void mnat_connected_handler(const struct sa *raddr1,
				   const struct sa *raddr2, void *arg)
{
	struct stream *strm = arg;

	info("stream: '%s' mnat '%s' connected: raddr %J %J\n",
	     media_name(strm->type),
	     strm->mnat->id, raddr1, raddr2);

	if (bundle_state(stream_bundle(strm)) == BUNDLE_MUX) {
		warning("stream: unexpected mnat connected"
			" in bundle state Mux\n");
		return;
	}

	strm->tx.raddr_rtp = *raddr1;

	if (strm->rtcp_mux)
		strm->tx.raddr_rtcp = *raddr1;
	else if (raddr2)
		strm->tx.raddr_rtcp = *raddr2;

	strm->mnat_connected = true;

	if (bundle_state(stream_bundle(strm)) == BUNDLE_BASE) {

		update_all_remote_addr(strm->le.list, raddr1);
	}

	if (strm->mnatconnh)
		strm->mnatconnh(strm, strm->sess_arg);

	if (bundle_state(stream_bundle(strm)) == BUNDLE_BASE) {

		struct le *le;

		for (le = strm->le.list->head; le; le = le->next) {
			struct stream *x = le->data;

			if (bundle_state(stream_bundle(x)) == BUNDLE_MUX) {

				x->mnat_connected = true;

				if (x->mnatconnh)
					x->mnatconnh(x, x->sess_arg);
			}
		}
	}
}


static int sender_init(struct sender *tx)
{
	int err;

	tx->metric = metric_alloc();
	if (!tx->metric)
		return ENOMEM;

	err = metric_init(tx->metric);

	tx->pt_enc = -1;

	return err;
}


static int receiver_init(struct receiver *rx)
{
	int err;

	rx->metric = metric_alloc();
	if (!rx->metric)
		return ENOMEM;

	err = metric_init(rx->metric);

	tmr_init(&rx->tmr_rtp);

	rx->pseq = -1;

	return err;
}


int stream_alloc(struct stream **sp, struct list *streaml,
		 const struct stream_param *prm,
		 const struct config_avt *cfg,
		 struct sdp_session *sdp_sess,
		 enum media_type type,
		 const struct mnat *mnat, struct mnat_sess *mnat_sess,
		 const struct menc *menc, struct menc_sess *menc_sess,
		 bool offerer,
		 stream_rtp_h *rtph, stream_rtcp_h *rtcph, stream_pt_h *pth,
		 void *arg)
{
	struct stream *s;
	int err;

	if (!sp || !prm || !cfg || !rtph || !pth)
		return EINVAL;

	s = mem_zalloc(sizeof(*s), stream_destructor);
	if (!s)
		return ENOMEM;

	MAGIC_INIT(s);

	err  = sender_init(&s->tx);
	err |= receiver_init(&s->rx);
	if (err)
		goto out;

	s->cfg = *cfg;
	s->cfg.rtcp_mux = prm->rtcp_mux;

	s->type   = type;
	s->rtph   = rtph;
	s->pth    = pth;
	s->rtcph  = rtcph;
	s->arg    = arg;
	s->ldir   = SDP_SENDRECV;

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

	if (prm->peer) {
		err = str_dup(&s->peer, prm->peer);
		if (err)
			goto out;
	}

	/* Jitter buffer */
	if (prm->use_rtp && cfg->jbtype != JBUF_OFF && cfg->jbuf_del.max) {

		err  = jbuf_alloc(&s->rx.jbuf, cfg->jbuf_del.min,
				cfg->jbuf_del.max);
		err |= jbuf_set_type(s->rx.jbuf, cfg->jbtype);
		if (err)
			goto out;
	}

	err = sdp_media_add(&s->sdp, sdp_sess, media_name(type),
			    s->rtp ? sa_port(rtp_local(s->rtp)) : PORT_DISCARD,
			    (menc && menc->sdp_proto) ? menc->sdp_proto :
			    sdp_proto_rtpavp);
	if (err)
		goto out;

	/* RFC 5506 */
	if (offerer || sdp_media_rattr(s->sdp, "rtcp-rsize"))
		err |= sdp_media_set_lattr(s->sdp, true, "rtcp-rsize", NULL);

	/* RFC 5576 */
	err |= sdp_media_set_lattr(s->sdp, true,
				   "ssrc", "%u cname:%s",
				   rtp_sess_ssrc(s->rtp), prm->cname);

	/* RFC 5761 */
	if (s->cfg.rtcp_mux &&
	    (offerer || sdp_media_rattr(s->sdp, "rtcp-mux"))) {

		err |= sdp_media_set_lattr(s->sdp, true, "rtcp-mux", NULL);
	}

	if (offerer) {
		uint32_t ix = list_count(streaml);

		err |= sdp_media_set_lattr(s->sdp, true, "mid", "%u",
					   ix);

		re_sdprintf(&s->mid, "%u", ix);
	}

	if (err)
		goto out;

	if (mnat && s->rtp) {
		s->mnat = mnat;
		err = mnat->mediah(&s->mns, mnat_sess,
				   rtp_sock(s->rtp),
				   s->cfg.rtcp_mux ? NULL : rtcp_sock(s->rtp),
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

	list_append(streaml, &s->le, s);

 out:
	if (err)
		mem_deref(s);
	else
		*sp = s;

	return err;
}


int stream_bundle_init(struct stream *strm, bool offerer)
{
	int err;

	if (!strm)
		return EINVAL;

	err = bundle_alloc(&strm->bundle);
	if (err)
		return err;

	if (offerer) {
		uint8_t id;

		id = stream_generate_extmap_id(strm);

		info("stream: bundle init offerer: generate id=%u\n", id);

		err = bundle_set_extmap(strm->bundle, strm->sdp, id);
		if (err)
			return err;
	}

	return 0;
}


uint8_t stream_generate_extmap_id(struct stream *strm)
{
	uint8_t id;

	if (!strm)
		return 0;

	id = ++strm->extmap_counter;

	if (id > RTPEXT_ID_MAX)
		return 0;

	return id;
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


/**
 * Write stream data to the network
 *
 * @param s		Stream object
 * @param ext		Extension bit
 * @param marker	Marker bit
 * @param pt		Payload type
 * @param ts		Timestamp
 * @param mb		Payload buffer
 *
 * @return int	0 if success, errorcode otherwise
 */
int stream_send(struct stream *s, bool ext, bool marker, int pt, uint32_t ts,
		struct mbuf *mb)
{
	int err = 0;

	if (!s)
		return EINVAL;

	if (!sa_isset(&s->tx.raddr_rtp, SA_ALL))
		return 0;

	if (!(sdp_media_rdir(s->sdp) & SDP_SENDONLY))
		return 0;

	if (sdp_media_ldir(s->sdp) == SDP_RECVONLY)
		return 0;

	if (sdp_media_ldir(s->sdp) == SDP_INACTIVE)
		return 0;

	if (s->hold)
		return 0;

	if (!stream_is_ready(s)) {
		warning("stream: send: not ready\n");
		return EINTR;
	}

	metric_add_packet(s->tx.metric, mbuf_get_left(mb));

	if (pt < 0)
		pt = s->tx.pt_enc;

	if (pt >= 0) {
		err = rtp_send(s->rtp, &s->tx.raddr_rtp, ext, marker, pt, ts,
			       tmr_jiffies_rt_usec(), mb);
		if (err)
			metric_inc_err(s->tx.metric);
	}

	return err;
}


static void disable_mnat(struct stream *s)
{
	info("stream: disable MNAT (%s)\n", media_name(s->type));

	s->mns = mem_deref(s->mns);
	s->mnat = NULL;
}


static void disable_menc(struct stream *strm)
{
	info("stream: disable MENC (%s)\n", media_name(strm->type));

	strm->mencs = mem_deref(strm->mencs);
	strm->menc = NULL;
}


static void update_remotes(struct list *streaml, const struct sa *raddr)
{
	struct le *le;

	for (le = streaml->head; le; le = le->next) {
		struct stream *strm = le->data;

		if (bundle_state(stream_bundle(strm)) == BUNDLE_MUX) {

			send_set_raddr(strm, raddr);
		}
	}
}


static void stream_remote_set(struct stream *s)
{
	const char *rmid, *rssrc;
	const struct network *net = baresip_network();

	if (!s)
		return;

	/* RFC 5576 */
	rssrc = sdp_media_rattr(s->sdp, "ssrc");
	if (rssrc) {
		struct pl num;

		if (0 == re_regex(rssrc, str_len(rssrc), "[0-9]+", &num))
			recv_set_ssrc(&s->rx, pl_u32(&num));
	}

	/* RFC 5761 */
	if (s->cfg.rtcp_mux && sdp_media_rattr(s->sdp, "rtcp-mux")) {

		if (!s->rtcp_mux)
			info("%s: RTP/RTCP multiplexing enabled\n",
			     sdp_media_name(s->sdp));
		s->rtcp_mux = true;

		sdp_media_set_lattr(s->sdp, true, "rtcp-mux", NULL);
	}

	/* RFC 5888 */
	rmid = sdp_media_rattr(s->sdp, "mid");
	if (rmid) {
		s->mid = mem_deref(s->mid);

		str_dup(&s->mid, rmid);

		sdp_media_set_lattr(s->sdp, true, "mid", "%s", rmid);
	}

	rtcp_enable_mux(s->rtp, s->rtcp_mux);

	if (bundle_state(stream_bundle(s)) != BUNDLE_MUX) {
		sa_cpy(&s->tx.raddr_rtp, sdp_media_raddr(s->sdp));

		if (s->rtcp_mux)
			s->tx.raddr_rtcp = s->tx.raddr_rtp;
		else
			sdp_media_raddr_rtcp(s->sdp, &s->tx.raddr_rtcp);
	}

	if (bundle_state(stream_bundle(s)) == BUNDLE_BASE) {

		update_remotes(s->le.list, &s->tx.raddr_rtp);
	}

	if (sa_af(&s->tx.raddr_rtp) == AF_INET6 &&
			sa_is_linklocal(&s->tx.raddr_rtp))
		net_set_dst_scopeid(net, &s->tx.raddr_rtp);

	if (sa_af(&s->tx.raddr_rtcp) == AF_INET6 &&
			sa_is_linklocal(&s->tx.raddr_rtcp))
		net_set_dst_scopeid(net, &s->tx.raddr_rtcp);
}


/**
 * Update the media stream
 *
 * @param s Stream object
 *
 * @return 0 if success, otherwise errorcode
 */
int stream_update(struct stream *s)
{
	const struct sdp_format *fmt;
	int err = 0;

	if (!s)
		return EINVAL;

	info("stream: update '%s'\n", media_name(s->type));

	fmt = sdp_media_rformat(s->sdp, NULL);

	s->tx.pt_enc = fmt ? fmt->pt : -1;

	if (sdp_media_has_media(s->sdp)) {

		if (bundle_state(s->bundle) == BUNDLE_MUX) {

			if (s->mnat)
				disable_mnat(s);
		}

		stream_remote_set(s);

		/* Bundle */
		if (s->bundle) {
			bundle_handle_extmap(s->bundle, s->sdp);
		}
	}

	if (s->mencs && mnat_ready(s)) {

		err = stream_start_mediaenc(s);
		if (err) {
			warning("stream: mediaenc update: %m\n", err);
			return err;
		}
	}

	return 0;
}


void stream_update_encoder(struct stream *s, int pt_enc)
{
	if (!s)
		return;

	if (pt_enc >= 0)
		s->tx.pt_enc = pt_enc;
}


void stream_hold(struct stream *s, bool hold)
{
	enum sdp_dir dir;

	if (!s)
		return;

	s->hold = hold;
	dir = s->ldir;

	if (hold) {
		switch (s->ldir) {
			case SDP_RECVONLY:
				dir = SDP_INACTIVE;
				break;
			case SDP_SENDRECV:
				dir = SDP_SENDONLY;
				break;
			default:
				break;
		}
	}

	sdp_media_set_ldir(s->sdp, dir);
	stream_flush(s);
}


void stream_set_ldir(struct stream *s, enum sdp_dir dir)
{
	if (!s)
		return;

	s->ldir = dir;

	if (dir == SDP_INACTIVE)
		sdp_media_set_disabled(s->sdp, true);
	else
		sdp_media_set_disabled(s->sdp, false);

	sdp_media_set_ldir(s->sdp, dir);

	stream_flush(s);
}


enum sdp_dir stream_ldir(const struct stream *s)
{
	return s ? s->ldir : SDP_INACTIVE;
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


void stream_flush(struct stream *s)
{
	if (!s)
		return;

	if (s->rx.jbuf)
		jbuf_flush(s->rx.jbuf);

	if (s->type == MEDIA_AUDIO)
		rtp_clear(s->rtp);
}


void stream_enable_rtp_timeout(struct stream *strm, uint32_t timeout_ms)
{
	if (!strm)
		return;

	if (!sdp_media_has_media(stream_sdpmedia(strm)))
		return;

	strm->rx.rtp_timeout = timeout_ms;

	tmr_cancel(&strm->rx.tmr_rtp);

	if (timeout_ms) {

		info("stream: Enable RTP timeout (%u milliseconds)\n",
		     timeout_ms);

		strm->rx.ts_last = tmr_jiffies();
		tmr_start(&strm->rx.tmr_rtp, 10, check_rtp_handler, strm);
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
	return strm ? metric_n_packets(strm->tx.metric) : 0;
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
	return strm ? metric_n_bytes(strm->tx.metric) : 0;
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
	return strm ? metric_n_err(strm->tx.metric) : 0;
}


/**
 * Get current transmitted RTP bitrate
 *
 * @param strm Stream object
 *
 * @return Current transmitted RTP bitrate
 */
uint32_t stream_metric_get_tx_bitrate(const struct stream *strm)
{
	return strm ? metric_bitrate(strm->tx.metric) : 0;
}


/**
 * Get average transmitted RTP bitrate
 *
 * @param strm Stream object
 *
 * @return Average transmitted RTP bitrate
 */
double stream_metric_get_tx_avg_bitrate(const struct stream *strm)
{
	return strm ? metric_avg_bitrate(strm->tx.metric) : 0.0;
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
	return strm ? metric_n_packets(strm->rx.metric) : 0;
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
	return strm ? metric_n_bytes(strm->rx.metric) : 0;
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
	return strm ? metric_n_err(strm->rx.metric) : 0;
}


/**
 * Get current received RTP bitrate
 *
 * @param strm Stream object
 *
 * @return Current received RTP bitrate
 */
uint32_t stream_metric_get_rx_bitrate(const struct stream *strm)
{
	return strm ? metric_bitrate(strm->rx.metric) : 0;
}


/**
 * Get average received RTP bitrate
 *
 * @param strm Stream object
 *
 * @return Average received RTP bitrate
 */
double stream_metric_get_rx_avg_bitrate(const struct stream *strm)
{
	return strm ? metric_avg_bitrate(strm->rx.metric) : 0.0;
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

	if (!sa_isset(&strm->tx.raddr_rtp, SA_ALL))
		return false;

	if (sdp_media_dir(stream_sdpmedia(strm)) == SDP_INACTIVE)
		return false;

	return !strm->terminated;
}


static void update_menc_muxed(struct list *streaml, bool secure)
{
	struct le *le;

	for (le = streaml->head; le; le = le->next) {
		struct stream *strm = le->data;

		if (bundle_state(stream_bundle(strm)) == BUNDLE_MUX) {

			debug("stream: update muxed: secure=%d\n", secure);
			strm->menc_secure = secure;
		}
	}
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

	if (bundle_state(stream_bundle(strm)) == BUNDLE_BASE) {

		update_menc_muxed(strm->le.list, secure);
	}
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
 * Start the media stream RTCP
 *
 * @param strm   Stream object
 *
 * @return 0 if success, otherwise errorcode
 */
int stream_start_rtcp(const struct stream *strm)
{
	int err;

	if (!strm)
		return EINVAL;

	debug("stream: %s: starting RTCP with remote %J\n",
	      media_name(strm->type), &strm->tx.raddr_rtcp);

	rtcp_start(strm->rtp, strm->cname, &strm->tx.raddr_rtcp);

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
 * Enable stream
 *
 * @param strm   Stream object
 * @param enable True to enable, false to disable
 *
 * @return 0 if success, otherwise errorcode
 */
int stream_enable(struct stream *strm, bool enable)
{
	if (!strm)
		return EINVAL;

	debug("stream: %s: %s RTP from remote\n", media_name(strm->type),
			enable ? "enable":"disable");

	strm->rx.enabled = enable;
	return 0;
}


/**
 * Open NAT-pinhole via RTP empty package
 *
 * @param strm	Stream object
 *
 * @return int 0 if success, otherwise errorcode
 */
int stream_open_natpinhole(const struct stream *strm)
{
	const struct sdp_format *sc = NULL;
	struct mbuf *mb = NULL;
	int err = 0;

	if (!strm)
		return EINVAL;

	if (!strm->mnat) {

		sc = sdp_media_rformat(strm->sdp, NULL);
		if (!sc)
			return EINVAL;

		mb = mbuf_alloc(RTP_HEADER_SIZE);
		if (!mb)
			return ENOMEM;

		mbuf_set_end(mb, RTP_HEADER_SIZE);
		mbuf_advance(mb, RTP_HEADER_SIZE);

		/* Send a dummy RTP packet to open NAT pinhole */
		err = rtp_send(strm->rtp, &strm->tx.raddr_rtp, false, false,
			       sc->pt, 0, tmr_jiffies_rt_usec(), mb);
		if (err) {
			warning("stream: rtp_send to open natpinhole"
				"failed (%m)\n", err);
			goto out;
		}
	}

 out:
	mem_deref(mb);
	return err;
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


/**
 * Get the value of the RTCP Canonical end-point identifier
 *
 * @param strm Stream object
 *
 * @return Canonical end-point identifier
 */
const char *stream_cname(const struct stream *strm)
{
	if (!strm)
		return NULL;

	return strm->cname;
}


/**
 * Get the peers URI/name or identifier
 *
 * @param strm Stream object
 *
 * @return Peers URI/name or identifier
 */
const char *stream_peer(const struct stream *strm)
{
	if (!strm)
		return NULL;

	return strm->peer;
}


const struct sa *stream_raddr(const struct stream *strm)
{
	if (!strm)
		return NULL;

	return &strm->tx.raddr_rtp;
}


enum media_type stream_type(const struct stream *strm)
{
	return strm ? strm->type : (enum media_type)-1;
}


int stream_pt_enc(const struct stream *strm)
{
	return strm ? strm->tx.pt_enc : -1;
}


struct rtp_sock *stream_rtp_sock(const struct stream *strm)
{
	return strm ? strm->rtp : NULL;
}


struct stream *stream_lookup_mid(const struct list *streaml,
				 const char *mid, size_t len)
{
	struct le *le;

	for (le = list_head(streaml); le; le = le->next) {
		struct stream *strm = le->data;

		if (len == str_len(strm->mid) &&
		    0 == memcmp(strm->mid, mid, len))
			return strm;
	}

	return NULL;
}


int stream_ssrc_rx(const struct stream *strm, uint32_t *ssrc)
{
	if (!strm || !ssrc)
		return EINVAL;

	if (strm->rx.ssrc_set) {
		*ssrc = strm->rx.ssrc;
		return 0;
	}
	else
		return ENOENT;
}


void stream_mnat_attr(struct stream *strm, const char *name, const char *value)
{
	if (!strm)
		return;

	if (strm->mnat && strm->mnat->attrh)
		strm->mnat->attrh(strm->mns, name, value);
}


const char *stream_mid(const struct stream *strm)
{
	return strm ? strm->mid : NULL;
}


struct bundle *stream_bundle(const struct stream *strm)
{
	return strm ? strm->bundle : NULL;
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

	err  = re_hprintf(pf, "--- Stream debug ---\n");
	err |= re_hprintf(pf, " %s dir=%s pt_enc=%d\n", sdp_media_name(s->sdp),
			  sdp_dir_name(sdp_media_dir(s->sdp)),
			  s->tx.pt_enc);

	err |= re_hprintf(pf, " local: %J, remote: %J/%J\n",
			  sdp_media_laddr(s->sdp),
			  &s->tx.raddr_rtp, &s->tx.raddr_rtcp);

	err |= re_hprintf(pf, " mnat: %s (connected=%s)\n",
			  s->mnat ? s->mnat->id : "(none)",
			  s->mnat_connected ? "yes" : "no");

	err |= re_hprintf(pf, " menc: %s (secure=%s)\n",
			  s->menc ? s->menc->id : "(none)",
			  s->menc_secure ? "yes" : "no");

	err |= re_hprintf(pf, " rx.enabled: %s\n",
			  s->rx.enabled ? "yes" : "no");

	err |= rtp_debug(pf, s->rtp);
	err |= jbuf_debug(pf, s->rx.jbuf);

	if (s->bundle)
		err |= bundle_debug(pf, s->bundle);

	return err;
}


int stream_print(struct re_printf *pf, const struct stream *s)
{
	if (!s)
		return 0;

	return re_hprintf(pf, " %s=%u/%u", sdp_media_name(s->sdp),
			  metric_bitrate(s->tx.metric),
			  metric_bitrate(s->rx.metric));
}


void stream_parse_mid(struct stream *strm)
{
	const char *rmid;

	if (!strm)
		return;

	/* RFC 5888 */
	rmid = sdp_media_rattr(strm->sdp, "mid");
	if (rmid) {

		if (str_isset(strm->mid)) {
			info("stream: parse mid: '%s' -> '%s'\n",
			     strm->mid, rmid);
		}

		strm->mid = mem_deref(strm->mid);

		str_dup(&strm->mid, rmid);

		sdp_media_set_lattr(strm->sdp, true, "mid", "%s", rmid);
	}
}


/* can be called after SDP o/a is complete */
void stream_enable_bundle(struct stream *strm, enum bundle_state st)
{
	if (!strm)
		return;

	info("stream: '%s' enable bundle (%s)\n",
	     media_name(strm->type), bundle_state_name(st));

	bundle_set_state(strm->bundle, st);

	if (st == BUNDLE_MUX) {

		if (strm->mnat)
			disable_mnat(strm);
		if (strm->menc)
			disable_menc(strm);
	}

	bundle_start_socket(strm->bundle, rtp_sock(strm->rtp), strm->le.list);
}
