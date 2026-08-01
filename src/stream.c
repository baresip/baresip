/**
 * @file stream.c  Generic Media Stream
 *
 * Copyright (C) 2010 Alfred E. Heggestad
 */
#include <string.h>
#include <time.h>
#include <re.h>
#include <re_atomic.h>
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
	RE_ATOMIC bool enabled;/**< True if enabled                 */
	mtx_t *lock;
};


/* Receive */
struct rtp_receiver;


struct rxmain {
	struct tmr tmr_rtp;    /**< Timer for detecting RTP timeout  */
	uint32_t rtp_timeout;  /**< RTP Timeout value in [ms]        */
	struct tmr tmr_rec;    /**< Timer for rtp_receiver start     */
	bool use_rxthread;     /**< Use RX thread flag               */
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
	struct menc_transport *menc_transport; /**< Shared DTLS transport   */
	enum media_type type;    /**< Media type, e.g. audio/video          */
	char *cname;             /**< RTCP Canonical end-point identifier   */
	char *peer;              /**< Peer URI/name or identifier           */
	char *mid;               /**< Media stream identification           */
	bool rtcp_mux;           /**< RTP/RTCP multiplex supported by peer  */
	bool terminated;         /**< Stream is terminated flag             */
	RE_ATOMIC bool hold;     /**< Stream is on-hold (local)             */
	bool mnat_connected;     /**< Media NAT is connected                */
	bool menc_secure;        /**< Media stream is secure                */
	bool native_mnat_suspended;
	bool native_mnat_prepared;
	bool native_mnat_activated;
	bool native_mnat_prepared_suspended;
	bool native_mnat_rollback_suspended;
	struct tmr tmr_natph;    /**< Timer for NAT pinhole                 */
	uint32_t natphc;         /**< NAT pinhole RTP counter               */
	bool pinhole;            /**< NAT pinhole flag                      */
	stream_rtcp_h *rtcph;    /**< Stream RTCP handler                   */
	void *arg;               /**< Handler argument                      */
	stream_mnatconn_h *mnatconnh;/**< Medianat connected handler        */
	stream_rtcp_h *sessrtcph;    /**< Stream RTCP handler               */
	stream_error_h *errorh;  /**< Stream error handler                  */
	void *sess_arg;          /**< Session handlers argument             */

	struct bundle *bundle;
	uint8_t extmap_counter;

	struct sender tx;

	struct rtp_receiver *rx;
	struct rxmain rxm;
};


/* Runtime fields derived from a remote description.  The snapshot owns all
 * references needed to restore without allocating. */
struct stream_jsep_state {
	struct stream *strm;
	char *mid;
	struct mnat_media *mns;
	struct menc_sess *mencs;
	struct menc_media *mes;
	struct menc_transport *menc_transport;
	const struct mnat *mnat;
	const struct menc *menc;
	struct sa raddr_rtp;
	struct sa raddr_rtcp;
	enum bundle_state bundle_state;
	uint8_t bundle_extmap_mid;
	int pt_enc;
	uint32_t rx_ssrc;
	bool tx_enabled;
	bool rx_ssrc_set;
	bool rx_enabled;
	bool rtcp_mux;
	bool mnat_connected;
	bool menc_secure;
};


static void stream_jsep_state_destructor(void *data)
{
	struct stream_jsep_state *state = data;

	mem_deref(state->strm);
	mem_deref(state->mid);
	mem_deref(state->mns);
	mem_deref(state->mencs);
	mem_deref(state->mes);
	mem_deref(state->menc_transport);
}


int stream_jsep_state_save(struct stream_jsep_state **statep,
			   struct stream *strm)
{
	struct stream_jsep_state *state;
	int err;

	if (!statep || !strm)
		return EINVAL;
	state = mem_zalloc(sizeof(*state), stream_jsep_state_destructor);
	if (!state)
		return ENOMEM;
	state->strm = mem_ref(strm);
	if (strm->mid) {
		err = str_dup(&state->mid, strm->mid);
		if (err) {
			mem_deref(state);
			return err;
		}
	}
	state->mns = mem_ref(strm->mns);
	state->mencs = mem_ref(strm->mencs);
	state->mes = mem_ref(strm->mes);
	state->menc_transport = mem_ref(strm->menc_transport);
	state->mnat = strm->mnat;
	state->menc = strm->menc;
	mtx_lock(strm->tx.lock);
	state->raddr_rtp = strm->tx.raddr_rtp;
	state->raddr_rtcp = strm->tx.raddr_rtcp;
	state->pt_enc = strm->tx.pt_enc;
	mtx_unlock(strm->tx.lock);
	state->tx_enabled = re_atomic_acq(&strm->tx.enabled);
	rtprecv_jsep_state_get(strm->rx, &state->rx_ssrc,
			       &state->rx_ssrc_set, &state->rx_enabled);
	state->rtcp_mux = strm->rtcp_mux;
	state->mnat_connected = strm->mnat_connected;
	state->menc_secure = strm->menc_secure;
	state->bundle_state = bundle_state(strm->bundle);
	state->bundle_extmap_mid = bundle_extmap_mid(strm->bundle);
	*statep = state;
	return 0;
}


void stream_jsep_state_restore(struct stream_jsep_state *state)
{
	struct stream *strm;
	char *mid;

	if (!state || !state->strm)
		return;
	strm = state->strm;
	mid = strm->mid;
	strm->mid = state->mid;
	state->mid = mid;
	mem_deref(strm->mns);
	strm->mns = state->mns;
	state->mns = NULL;
	mem_deref(strm->mencs);
	strm->mencs = state->mencs;
	state->mencs = NULL;
	mem_deref(strm->mes);
	strm->mes = state->mes;
	state->mes = NULL;
	mem_deref(strm->menc_transport);
	strm->menc_transport = state->menc_transport;
	state->menc_transport = NULL;
	strm->mnat = state->mnat;
	strm->menc = state->menc;
	mtx_lock(strm->tx.lock);
	strm->tx.raddr_rtp = state->raddr_rtp;
	strm->tx.raddr_rtcp = state->raddr_rtcp;
	strm->tx.pt_enc = state->pt_enc;
	mtx_unlock(strm->tx.lock);
	re_atomic_rls_set(&strm->tx.enabled, state->tx_enabled);
	rtprecv_jsep_state_restore(strm->rx, state->rx_ssrc,
				   state->rx_ssrc_set, state->rx_enabled);
	strm->rtcp_mux = state->rtcp_mux;
	rtprecv_enable_mux(strm->rx, state->rtcp_mux);
	strm->mnat_connected = state->mnat_connected;
	strm->menc_secure = state->menc_secure;
	bundle_set_state(strm->bundle, state->bundle_state);
	/* Restoring the negotiated MID extension is allocation-free. */
	if (strm->bundle)
		bundle_restore_extmap(strm->bundle, state->bundle_extmap_mid);
}


static void print_rtp_stats(const struct stream *s)
{
	uint32_t tx_n_packets = metric_n_packets(s->tx.metric);
	uint32_t rx_n_packets = metric_n_packets(rtprecv_metric(s->rx));
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
	     1.0*metric_avg_bitrate(rtprecv_metric(s->rx))/1000.0,
	     metric_n_err(s->tx.metric),
	     metric_n_err(rtprecv_metric(s->rx))
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

	tmr_cancel(&s->rxm.tmr_rtp);
	tmr_cancel(&s->rxm.tmr_rec);
	tmr_cancel(&s->tmr_natph);
	mem_deref(s->rx);
	list_unlink(&s->le);
	mem_deref(s->sdp);
	mem_deref(s->mes);
	mem_deref(s->menc_transport);
	mem_deref(s->mencs);
	mem_deref(s->mns);
	mem_deref(s->bundle);  /* NOTE: deref before rtp */
	mem_deref(s->rtp);
	mem_deref(s->cname);
	mem_deref(s->peer);
	mem_deref(s->mid);
	mem_deref(s->tx.lock);
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

	mtx_lock(strm->tx.lock);
	strm->tx.raddr_rtp  = *raddr;
	strm->tx.raddr_rtcp = *raddr;
	mtx_unlock(strm->tx.lock);
}


static bool mnat_ready(const struct stream *strm)
{
	if (bundle_state(stream_bundle(strm)) == BUNDLE_MUX)
		return strm->mnat_connected;

	if (strm->mnat && strm->mnat->wait_connected)
		return strm->mnat_connected;
	else
		return true;
}


/**
 * Enable TX stream
 *
 * @param strm   Stream object
 * @param enable True to enable, false to disable
 *
 * @return 0 if success and otherwise errorcode
 */
int stream_enable_tx(struct stream *strm, bool enable)
{
	if (!strm)
		return EINVAL;

	if (!enable) {
		debug("stream: disable %s RTP sender\n",
		      media_name(strm->type));

		re_atomic_rls_set(&strm->tx.enabled, false);
		return 0;
	}

	if (!stream_is_ready(strm))
		return EAGAIN;

	if (!(sdp_media_rdir(strm->sdp) & SDP_SENDONLY))
		return ENOTSUP;

	if (sdp_media_ldir(strm->sdp) == SDP_RECVONLY)
		return ENOTSUP;

	if (sdp_media_ldir(strm->sdp) == SDP_INACTIVE)
		return ENOTSUP;

	debug("stream: enable %s RTP sender\n", media_name(strm->type));
	re_atomic_rls_set(&strm->tx.enabled, true);

	return 0;
}


static void stream_start_receiver(void *arg)
{
	struct stream *s = arg;
	rtprecv_start_thread(s->rx);
}


/**
 * Enable RX stream
 *
 * @param strm   Stream object
 * @param enable True to enable, false to disable
 *
 * @return 0 if success and otherwise errorcode
 */
int stream_enable_rx(struct stream *strm, bool enable)
{
	if (!strm)
		return EINVAL;

	if (!enable) {
		debug("stream: disable %s RTP receiver\n",
		      media_name(strm->type));

		rtprecv_enable(strm->rx, false);
		return 0;
	}

	if (!(sdp_media_dir(strm->sdp) & SDP_RECVONLY))
		return ENOTSUP;

	debug("stream: enable %s RTP receiver\n", media_name(strm->type));
	rtprecv_enable(strm->rx, true);

	if (strm->rtp && strm->cfg.rxmode == RECEIVE_MODE_THREAD &&
	    strm->type == MEDIA_AUDIO && !rtprecv_running(strm->rx)) {
		if (stream_bundle(strm)) {
			warning("stream: rtp_rxmode thread was disabled "
				"because it is not supported in combination "
				"with avt_bundle\n");
		}
		else {
			strm->rxm.use_rxthread = true;
			tmr_start(&strm->rxm.tmr_rec, 1, stream_start_receiver,
				  strm);
		}
	}

	return 0;
}


static void stream_close(struct stream *strm, int err)
{
	stream_error_h *errorh = strm->errorh;

	strm->terminated = true;
	stream_enable(strm, false);
	strm->errorh = NULL;

	strm->rx = mem_deref(strm->rx);
	if (errorh)
		errorh(strm, err, strm->sess_arg);
}


static void check_rtp_handler(void *arg)
{
	struct stream *strm = arg;
	const uint64_t now = tmr_jiffies();
	uint64_t ts_last;
	int diff_ms;

	MAGIC_CHECK(strm);

	tmr_start(&strm->rxm.tmr_rtp, RTP_CHECK_INTERVAL,
		  check_rtp_handler, strm);

	/* If no RTP was received at all, check later */
	ts_last = rtprecv_ts_last(strm->rx);
	if (!ts_last)
		return;

	/* We are in sendrecv mode, check when the last RTP packet
	 * was received.
	 */
	if (sdp_media_dir(strm->sdp) == SDP_SENDRECV) {

		diff_ms = (int)(now - ts_last);

		if (diff_ms > 100) {
			debug("stream: last \"%s\" RTP packet: %d "
			      "milliseconds\n",
			      sdp_media_name(strm->sdp), diff_ms);
		}

		/* check for large jumps in time */
		if (diff_ms > (3600 * 1000)) {
			rtprecv_set_ts_last(strm->rx, 0);
			return;
		}

		if (diff_ms > (int)strm->rxm.rtp_timeout) {

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


void stream_process_rtcp(struct stream *strm, struct rtcp_msg *msg)
{
	if (msg->hdr.pt == RTCP_SR && msg->hdr.count) {
		(void)rtcp_stats(strm->rtp, msg->r.sr.ssrc, &strm->rtcp_stats);
	}
	else if (msg->hdr.pt == RTCP_RR) { /* maybe rtx.ssrc RFC 4588 */
		(void)rtcp_stats(strm->rtp, msg->r.rr.ssrc, &strm->rtcp_stats);
	}

	if (strm->rtcph)
		strm->rtcph(strm, msg, strm->arg);

	if (strm->sessrtcph)
		strm->sessrtcph(strm, msg, strm->sess_arg);
}


static int stream_sock_alloc(struct stream *s, int af)
{
	struct sa laddr;
	uint8_t tos;
	int err;

	if (!s)
		return EINVAL;

	/* we listen on all interfaces */
	sa_init(&laddr, af);

	err = rtp_listen(&s->rtp, IPPROTO_UDP, &laddr,
			 s->cfg.rtp_ports.min, s->cfg.rtp_ports.max,
			 true, rtprecv_decode, rtprecv_handle_rtcp, s->rx);
	if (err) {
		warning("stream: rtp_listen failed: af=%s ports=%u-%u"
			" (%m)\n", net_af2name(af),
			s->cfg.rtp_ports.min, s->cfg.rtp_ports.max, err);
		return err;
	}

	tos = s->type == MEDIA_AUDIO ? s->cfg.rtp_tos : s->cfg.rtpv_tos;
	(void)udp_settos(rtp_sock(s->rtp), tos);
	(void)udp_settos(rtcp_sock(s->rtp), tos);

	udp_rxsz_set(rtp_sock(s->rtp), RTP_RECV_SIZE);

	if (s->type == MEDIA_VIDEO)
		udp_sockbuf_set(rtp_sock(s->rtp), 65536 * 8);
	else
		udp_sockbuf_set(rtp_sock(s->rtp), 65536);

	rtprecv_set_socket(s->rx, s->rtp);
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
		struct sa raddr_rtp;
		struct sa raddr_rtcp;

		info("stream: %s: starting mediaenc '%s' (wait_secure=%d)\n",
		     media_name(strm->type), strm->menc->id,
		     strm->menc->wait_secure);

		mtx_lock(strm->tx.lock);
		sa_cpy(&raddr_rtp,  &strm->tx.raddr_rtp);
		sa_cpy(&raddr_rtcp, &strm->tx.raddr_rtcp);
		mtx_unlock(strm->tx.lock);

		err = strm->menc->mediah(&strm->mes, strm->mencs,
				 strm->menc_transport,
				 strm->rtp,
				 rtp_sock(strm->rtp),
				 strm->rtcp_mux ? NULL : rtcp_sock(strm->rtp),
				 &raddr_rtp,
				 strm->rtcp_mux ? NULL : &raddr_rtcp,
					 strm->sdp, strm);
		if (err) {
			warning("stream: start mediaenc error: %m\n", err);
			return err;
		}
	}

	return 0;
}


int stream_prepare_menc_transport(struct stream *strm,
				  struct menc_transport *transport)
{
	return stream_prepare_menc_transport_mux(
		strm, transport,
		strm && (strm->rtcp_mux || !rtcp_sock(strm->rtp)));
}


int stream_prepare_menc_transport_mux(struct stream *strm,
				      struct menc_transport *transport,
				      bool rtcp_mux)
{
	if (!strm || !transport)
		return EINVAL;
	if (!strm->menc || !strm->mes ||
	    !strm->menc->transportmemberaddh)
		return ENOTSUP;

	return menc_transport_member_add(strm->menc, transport, strm->mes,
					 rtcp_mux || !rtcp_sock(strm->rtp));
}


int stream_prepare_menc_transport_removal(
	struct stream *strm, struct menc_transport *transaction)
{
	if (!strm || !transaction)
		return EINVAL;
	if (!strm->menc || !strm->mes)
		return ENOTSUP;

	return menc_transport_member_remove(strm->menc, transaction,
					    strm->mes);
}


void stream_publish_menc_transport(struct stream *strm,
				   struct menc_transport *transport)
{
	stream_set_menc_transport(strm, transport);
}


void stream_set_menc_transport(struct stream *strm,
			       struct menc_transport *transport)
{
	if (!strm || strm->menc_transport == transport)
		return;

	mem_deref(strm->menc_transport);
	strm->menc_transport = mem_ref(transport);
}


struct menc_transport *stream_menc_transport_ref(
	const struct stream *strm)
{
	return strm ? mem_ref(strm->menc_transport) : NULL;
}


bool stream_has_menc_transport(const struct stream *strm)
{
	return strm && strm->menc_transport;
}


struct mnat_media *stream_mnat_media_ref(const struct stream *strm)
{
	return strm ? mem_ref(strm->mns) : NULL;
}


void stream_publish_mnat_media(struct stream *strm,
			       struct mnat_media *mnat_media)
{
	struct mnat_media *previous;

	if (!strm || strm->mns == mnat_media)
		return;
	previous = strm->mns;
	strm->mns = mem_ref(mnat_media);
	mem_deref(previous);
}


int stream_set_mid(struct stream *strm, const char *mid)
{
	char *value;
	int err;

	if (!strm || !str_isset(mid))
		return EINVAL;
	if (!str_cmp(strm->mid, mid))
		return 0;

	err = str_dup(&value, mid);
	if (err)
		return err;
	err = sdp_media_set_lattr(strm->sdp, true, "mid", "%s", mid);
	if (err) {
		mem_deref(value);
		return err;
	}

	mem_deref(strm->mid);
	strm->mid = value;
	return 0;
}


int stream_promote_menc_transport(
	struct stream *strm, struct menc_transport **mtp,
	menc_transport_recv_h *recvh, menc_transport_estab_h *estabh,
	menc_transport_close_h *closeh, void *arg)
{
	if (!strm || !mtp)
		return EINVAL;
	if (!strm->menc || !strm->menc->transportpromoteh || !strm->mes)
		return ENOTSUP;

	return strm->menc->transportpromoteh(mtp, strm->mes, recvh, estabh,
					     closeh, arg);
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


void stream_mnat_connected(struct stream *strm, const struct sa *raddr1,
			   const struct sa *raddr2)
{
	info("stream: '%s' mnat '%s' connected: raddr %J %J\n",
	     media_name(strm->type),
	     strm->mnat->id, raddr1, raddr2);

	if (bundle_state(stream_bundle(strm)) == BUNDLE_MUX) {
		warning("stream: unexpected mnat connected"
			" in bundle state Mux\n");
		return;
	}

	mtx_lock(strm->tx.lock);
	strm->tx.raddr_rtp = *raddr1;

	if (strm->rtcp_mux)
		strm->tx.raddr_rtcp = *raddr1;
	else if (raddr2)
		strm->tx.raddr_rtcp = *raddr2;
	mtx_unlock(strm->tx.lock);

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

	stream_enable_tx(strm, true);
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

	s = mem_zalloc(sizeof(*s), NULL);
	if (!s)
		return ENOMEM;

	MAGIC_INIT(s);

	err = mutex_alloc(&s->tx.lock);
	if (err)
		goto out;

	mem_destructor(s, stream_destructor);

	err  = sender_init(&s->tx);
	if (err)
		goto out;

	s->cfg = *cfg;
	s->cfg.rtcp_mux = prm->rtcp_mux;

	s->type   = type;
	s->rtcph  = rtcph;
	s->arg    = arg;
	s->ldir   = SDP_SENDRECV;
	s->pinhole = true;
	tmr_init(&s->tmr_natph);

	if (prm->use_rtp) {
		err = rtprecv_alloc(&s->rx, s, media_name(type), cfg,
			       rtph, pth, arg);
		if (err) {
			warning("stream: failed to create receiver"
				" for media '%s' (%m)\n",
				media_name(type), err);
			goto out;
		}

		tmr_init(&s->rxm.tmr_rtp);
		tmr_init(&s->rxm.tmr_rec);
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
				   s->sdp,
				   rtprecv_mnat_connected_handler, s->rx);
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

	err = bundle_alloc(&strm->bundle, strm);
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

	if (!re_atomic_acq(&s->tx.enabled))
		return 0;

	if (re_atomic_rlx(&s->hold))
		return 0;

	metric_add_packet(s->tx.metric, mbuf_get_left(mb));

	if (pt < 0) {
		mtx_lock(s->tx.lock);
		pt = s->tx.pt_enc;
		mtx_unlock(s->tx.lock);
	}

	if (pt >= 0) {
		mtx_lock(s->tx.lock);
		err = rtp_send(s->rtp, &s->tx.raddr_rtp, ext, marker, pt, ts,
			       tmr_jiffies_rt_usec(), mb);
		mtx_unlock(s->tx.lock);
		if (err)
			metric_inc_err(s->tx.metric);
	}

	return err;
}


/**
 * Write stream data to the network
 *
 * @param s		Stream object
 * @param seq		Sequence
 * @param ext		Extension bit
 * @param marker	Marker bit
 * @param pt		Payload type
 * @param ts		Timestamp
 * @param mb		Payload buffer
 *
 * @return int	0 if success, errorcode otherwise
 */
int stream_resend(struct stream *s, uint16_t seq, bool ext, bool marker,
		  int pt, uint32_t ts, struct mbuf *mb)
{
	struct sa raddr_rtp;

	mtx_lock(s->tx.lock);
	sa_cpy(&raddr_rtp,  &s->tx.raddr_rtp);
	mtx_unlock(s->tx.lock);
	return rtp_resend(s->rtp, seq, &raddr_rtp, ext, marker, pt, ts, mb);
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


int stream_native_mnat_prepare(struct stream *strm, bool standalone)
{
	bool suspend;
	int err = 0;

	if (!strm)
		return EINVAL;
	if (strm->native_mnat_prepared)
		return strm->native_mnat_prepared_suspended == !standalone
			       ? 0 : EBUSY;
	if (strm->native_mnat_activated)
		return EALREADY;

	suspend = !standalone;
	if (strm->mnat && strm->mns) {
		if (strm->mnat->mediaprepareh &&
		    strm->mnat->mediaactivateh &&
		    strm->mnat->mediarollbackh &&
		    strm->mnat->mediafinalizeh &&
		    strm->mnat->mediaaborth) {
			err = strm->mnat->mediaprepareh(strm->mns, !suspend);
			if (err)
				return err;
		}
		else if (suspend != strm->native_mnat_suspended) {
			return ENOTSUP;
		}
	}

	strm->native_mnat_prepared_suspended = suspend;
	strm->native_mnat_prepared = true;
	return 0;
}


void stream_native_mnat_activate(struct stream *strm)
{
	if (!strm || !strm->native_mnat_prepared || strm->native_mnat_activated)
		return;

	strm->native_mnat_rollback_suspended = strm->native_mnat_suspended;
	if (strm->mnat && strm->mns && strm->mnat->mediaactivateh)
		strm->mnat->mediaactivateh(strm->mns);
	strm->native_mnat_suspended = strm->native_mnat_prepared_suspended;
	strm->native_mnat_prepared = false;
	strm->native_mnat_activated = true;
}


void stream_native_mnat_rollback(struct stream *strm)
{
	if (!strm || !strm->native_mnat_activated)
		return;

	if (strm->mnat && strm->mns && strm->mnat->mediarollbackh)
		strm->mnat->mediarollbackh(strm->mns);
	strm->native_mnat_suspended = strm->native_mnat_rollback_suspended;
	strm->native_mnat_activated = false;
}


void stream_native_mnat_finalize(struct stream *strm)
{
	if (!strm || !strm->native_mnat_activated)
		return;

	if (strm->mnat && strm->mns && strm->mnat->mediafinalizeh)
		strm->mnat->mediafinalizeh(strm->mns);
	strm->native_mnat_activated = false;
}


void stream_native_mnat_abort(struct stream *strm)
{
	if (!strm || !strm->native_mnat_prepared || strm->native_mnat_activated)
		return;

	if (strm->mnat && strm->mns && strm->mnat->mediaaborth)
		strm->mnat->mediaaborth(strm->mns);
	strm->native_mnat_prepared = false;
}


bool stream_native_mnat_suspended(const struct stream *strm)
{
	return strm && strm->native_mnat_suspended;
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


static int stream_remote_set(struct stream *s)
{
	const char *rssrc;
	const struct network *net = baresip_network();
	bool rtcp_mux;
	int err;

	if (!s)
		return EINVAL;

	/* RFC 5576 */
	rssrc = sdp_media_rattr(s->sdp, "ssrc");
	if (rssrc) {
		struct pl num;

		if (0 == re_regex(rssrc, str_len(rssrc), "[0-9]+", &num))
			rtprecv_set_ssrc(s->rx, pl_u32(&num));
	}

	/* RFC 5761 */
	rtcp_mux = s->cfg.rtcp_mux && sdp_media_rattr(s->sdp, "rtcp-mux");
	if (rtcp_mux) {

		if (!s->rtcp_mux)
			info("%s: RTP/RTCP multiplexing enabled\n",
			     sdp_media_name(s->sdp));
		err = sdp_media_set_lattr(s->sdp, true, "rtcp-mux", NULL);
		if (err)
			return err;
	}
	else
		sdp_media_del_lattr(s->sdp, "rtcp-mux");
	s->rtcp_mux = rtcp_mux;

	rtprecv_enable_mux(s->rx, s->rtcp_mux);

	mtx_lock(s->tx.lock);
	if (bundle_state(stream_bundle(s)) != BUNDLE_MUX) {
		sa_cpy(&s->tx.raddr_rtp, sdp_media_raddr(s->sdp));

		if (s->rtcp_mux)
			s->tx.raddr_rtcp = s->tx.raddr_rtp;
		else
			sdp_media_raddr_rtcp(s->sdp, &s->tx.raddr_rtcp);
	}
	mtx_unlock(s->tx.lock);

	if (bundle_state(stream_bundle(s)) == BUNDLE_BASE) {

		update_remotes(s->le.list, &s->tx.raddr_rtp);
	}

	mtx_lock(s->tx.lock);
	if (sa_af(&s->tx.raddr_rtp) == AF_INET6 &&
			sa_is_linklocal(&s->tx.raddr_rtp))
		net_set_dst_scopeid(net, &s->tx.raddr_rtp);

	if (sa_af(&s->tx.raddr_rtcp) == AF_INET6 &&
			sa_is_linklocal(&s->tx.raddr_rtcp))
		net_set_dst_scopeid(net, &s->tx.raddr_rtcp);
	mtx_unlock(s->tx.lock);
	return 0;
}


/**
 * Update the media stream
 *
 * @param s Stream object
 *
 * @return 0 if success, otherwise errorcode
 */
static int stream_update_internal(struct stream *s, bool start_menc)
{
	const struct sdp_format *fmt;
	int err = 0;

	if (!s)
		return EINVAL;

	info("stream: update '%s'\n", media_name(s->type));

	/* disable rx/tx stream for updates */
	stream_enable(s, false);

	fmt = sdp_media_rformat(s->sdp, NULL);

	mtx_lock(s->tx.lock);
	s->tx.pt_enc = fmt ? fmt->pt : -1;
	mtx_unlock(s->tx.lock);

	if (sdp_media_has_media(s->sdp)) {

		/* A muxed stream keeps its native transport dormant so a later
		 * BUNDLE split can reactivate it without rebuilding ownership. */

		err = stream_remote_set(s);
		if (err)
			return err;

		/* Bundle */
		if (s->bundle) {
			err = bundle_handle_extmap(s->bundle, s->sdp);
			if (err)
				return err;
		}
	}

	if (start_menc && s->mencs && mnat_ready(s) &&
	    (!s->native_mnat_suspended || s->menc_transport)) {

		err = stream_start_mediaenc(s);
		if (err) {
			warning("stream: mediaenc update: %m\n", err);
			return err;
		}
	}

	stream_enable(s, true);

	return 0;
}


int stream_update(struct stream *s)
{
	return stream_update_internal(s, true);
}


int stream_update_jsep(struct stream *s)
{
	/* Transport establishment is intentionally outside the description
	 * transaction.  In particular, mediah may synchronously publish secure
	 * events and invoke application callbacks, which cannot be rolled back. */
	return stream_update_internal(s, false);
}


/**
 * Calls the transmission rekeying handler of the media encryption
 *
 * @param strm Stream to rekey
 */
void stream_remove_menc_media_state(struct stream *strm)
{
	if (!strm)
		return;

	if (strm->menc->txrekeyh)
		strm->menc->txrekeyh(strm->mes);
}


void stream_update_encoder(struct stream *s, int pt_enc)
{
	if (!s)
		return;

	if (pt_enc >= 0) {
		mtx_lock(s->tx.lock);
		s->tx.pt_enc = pt_enc;
		mtx_unlock(s->tx.lock);
	}
}


void stream_hold(struct stream *s, bool hold)
{
	enum sdp_dir dir;

	if (!s)
		return;

	re_atomic_rlx_set(&s->hold, hold);
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


void stream_set_rtcp_interval(struct stream *s, uint32_t n)
{
	if (!s)
		return;

	rtcp_set_interval(s->rtp, n);
}


void stream_set_srate(struct stream *s, uint32_t srate_tx, uint32_t srate_rx)
{
	if (!s)
		return;

	if (srate_tx)
		rtcp_set_srate_tx(s->rtp, srate_tx);
	if (srate_rx) {
		rtcp_set_srate_rx(s->rtp, srate_rx);
		rtprecv_set_srate(s->rx, srate_rx);
	}
}


void stream_flush(struct stream *s)
{
	if (!s)
		return;

	rtprecv_flush(s->rx);

	if (s->type == MEDIA_AUDIO)
		rtp_clear(s->rtp);
}


void stream_stop(struct stream *s)
{
	if (!s)
		return;

	stream_enable(s, false);
	bundle_stop(s->bundle);
	rtcp_start(s->rtp, NULL, NULL);
	rtprecv_stop(s->rx);
}


void stream_enable_rtp_timeout(struct stream *strm, uint32_t timeout_ms)
{
	struct sdp_media *m;

	if (!strm)
		return;

	m = stream_sdpmedia(strm);
	if (!sdp_media_has_media(m))
		return;

	if (sdp_media_disabled(m))
		return;

	const struct sdp_format *sc = sdp_media_rformat(m, NULL);
	if (!sc || !sc->data)
		return;

	strm->rxm.rtp_timeout = timeout_ms;

	tmr_cancel(&strm->rxm.tmr_rtp);

	if (timeout_ms) {

		info("stream: Enable RTP timeout (%u milliseconds)\n",
		     timeout_ms);

		rtprecv_set_ts_last(strm->rx, tmr_jiffies());
		tmr_start(&strm->rxm.tmr_rtp, 10, check_rtp_handler, strm);
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
	strm->sessrtcph  = rtcph;
	strm->errorh     = errorh;
	strm->sess_arg   = arg;

	rtprecv_set_handlers(strm->rx, rtpestabh, arg);
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
 * Get the Jitter Buffer Statistics from a media stream
 *
 * @param strm Stream object
 * @param stat Pointer to statistics storage
 *
 * @return 0 if success, otherwise errorcode
 */
int stream_jbuf_stats(const struct stream *strm, struct jbuf_stat *stat)
{
	if (!strm)
		return EINVAL;

	return jbuf_stats(rtprecv_jbuf(strm->rx), stat);
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
	return strm ? metric_n_packets(rtprecv_metric(strm->rx)) : 0;
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
	return strm ? metric_n_bytes(rtprecv_metric(strm->rx)) : 0;
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
	return strm ? metric_n_err(rtprecv_metric(strm->rx)) : 0;
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
	return strm ? metric_bitrate(rtprecv_metric(strm->rx)) : 0;
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
	return strm ? metric_avg_bitrate(rtprecv_metric(strm->rx)) : 0.0;
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

	mtx_lock(strm->tx.lock);
	if (!sa_isset(&strm->tx.raddr_rtp, SA_ALL)) {
		mtx_unlock(strm->tx.lock);
		return false;
	}
	mtx_unlock(strm->tx.lock);

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


static uint32_t phwait(struct stream *strm)
{
	if (strm->natphc < 6)
		++strm->natphc;

	return 10 * (1 << strm->natphc);
}


static void natpinhole_handler(void *arg)
{
	struct stream *strm = arg;
	const struct sdp_format *sc = NULL;
	struct mbuf *mb = NULL;
	struct sa raddr_rtp;
	int err = 0;

	sc = sdp_media_rformat(strm->sdp, NULL);
	if (!sc)
		return;

	mb = mbuf_alloc(RTP_HEADER_SIZE);
	if (!mb)
		return;

	tmr_start(&strm->tmr_natph, phwait(strm), natpinhole_handler, strm);
	mbuf_set_end(mb, RTP_HEADER_SIZE);
	mbuf_advance(mb, RTP_HEADER_SIZE);

	mtx_lock(strm->tx.lock);
	sa_cpy(&raddr_rtp,  &strm->tx.raddr_rtp);
	mtx_unlock(strm->tx.lock);

	/* Send a dummy RTP packet to open NAT pinhole */
	err = rtp_send(strm->rtp, &raddr_rtp, false, false,
		       sc->pt, 0, tmr_jiffies_rt_usec(), mb);
	if (err) {
		warning("stream: rtp_send to open natpinhole"
			"failed (%m)\n", err);
	}

	mem_deref(mb);
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

	stream_enable_tx(strm, true);
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
	int err = 0;

	if (!strm)
		return EINVAL;

	debug("stream: %s: starting RTCP with remote %J\n",
	      media_name(strm->type), &strm->tx.raddr_rtcp);

	if (strm->rxm.use_rxthread) {
		err = rtprecv_start_rtcp(strm->rx, strm->cname,
					 &strm->tx.raddr_rtcp, !strm->mnat);
	}
	else {
		rtcp_start(strm->rtp, strm->cname, &strm->tx.raddr_rtcp);

		if (!strm->mnat) {
			/* Send a dummy RTCP packet to open NAT pinhole */
			err = rtcp_send_app(strm->rtp, "PING",
					    (void *)"PONG", 4);
			if (err) {
				warning("stream: rtcp_send_app failed (%m)\n",
					err);
				return err;
			}
		}
	}

	return err;
}


/**
 * Enable stream (RX and TX)
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

	stream_enable_rx(strm, enable);
	stream_enable_tx(strm, enable);
	return 0;
}


/**
 * Open NAT-pinhole via RTP empty package
 *
 * @param strm	Stream object
 */
void stream_open_natpinhole(struct stream *strm)
{
	if (!strm)
		return;

	if (strm->pinhole)
		tmr_start(&strm->tmr_natph, 10, natpinhole_handler, strm);
}


void stream_stop_natpinhole(struct stream *strm)
{
	if (!strm)
		return;

	tmr_cancel(&strm->tmr_natph);
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
	int pt;


	if (!strm)
		return -1;

	mtx_lock(strm->tx.lock);
	pt =  strm->tx.pt_enc;
	mtx_unlock(strm->tx.lock);

	return pt;
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
	if (!strm)
		return EINVAL;

	return rtprecv_get_ssrc(strm->rx, ssrc);
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


static int mbuf_print_h(const char *p, size_t size, void *arg)
{
	struct mbuf *mb = arg;

	return mbuf_write_mem(mb, (const uint8_t *) p, size);
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
	struct mbuf *mb;
	int err;
	struct re_printf pfmb;
	if (!s)
		return 0;

	mb = mbuf_alloc(64);
	if (!mb) {
		err = ENOMEM;
		goto out;
	}

	pfmb.vph = mbuf_print_h;
	pfmb.arg = mb;
	err  = mbuf_printf(mb, "--- Stream debug ---\n");
	mtx_lock(s->tx.lock);
	err |= mbuf_printf(mb, " %s dir=%s pt_enc=%d\n",
			   sdp_media_name(s->sdp),
			   sdp_dir_name(sdp_media_dir(s->sdp)),
			   s->tx.pt_enc);

	err |= mbuf_printf(mb, " local: %J, remote: %J/%J\n",
			   sdp_media_laddr(s->sdp),
			   &s->tx.raddr_rtp, &s->tx.raddr_rtcp);

	err |= mbuf_printf(mb, " mnat: %s (connected=%s)\n",
			   s->mnat ? s->mnat->id : "(none)",
			   s->mnat_connected ? "yes" : "no");

	err |= mbuf_printf(mb, " menc: %s (secure=%s)\n",
			   s->menc ? s->menc->id : "(none)",
			   s->menc_secure ? "yes" : "no");

	err |= mbuf_printf(mb, " tx.enabled: %s\n",
			   re_atomic_rlx(&s->tx.enabled) ? "yes" : "no");
	err |= rtprecv_debug(&pfmb, s->rx);
	err |= rtp_debug(&pfmb, s->rtp);

	if (s->bundle)
		err |= bundle_debug(&pfmb, s->bundle);

	mtx_unlock(s->tx.lock);
	if (err)
		goto out;

	err = re_hprintf(pf, "%b", mb->buf, mb->pos);
out:
	mem_deref(mb);
	return err;
}


int stream_print(struct re_printf *pf, const struct stream *s)
{
	if (!s)
		return 0;

	return re_hprintf(pf, " %s=%u/%u", sdp_media_name(s->sdp),
			  metric_bitrate(s->tx.metric),
			  metric_bitrate(rtprecv_metric(s->rx)));
}


int stream_parse_mid(struct stream *strm)
{
	const char *rmid;
	char *mid = NULL;
	int err;

	if (!strm)
		return EINVAL;

	/* RFC 5888 */
	rmid = sdp_media_rattr(strm->sdp, "mid");
	if (rmid) {

		if (str_isset(strm->mid)) {
			info("stream: parse mid: '%s' -> '%s'\n",
			     strm->mid, rmid);
		}

		err = str_dup(&mid, rmid);
		if (err)
			return err;
		err = sdp_media_set_lattr(strm->sdp, true, "mid", "%s", rmid);
		if (err) {
			mem_deref(mid);
			return err;
		}
		mem_deref(strm->mid);
		strm->mid = mid;
	}
	return 0;
}


/* can be called after SDP o/a is complete */
void stream_enable_bundle(struct stream *strm, enum bundle_state st)
{
	enum bundle_state previous;
	int err;

	if (!strm)
		return;

	info("stream: '%s' enable bundle (%s)\n",
	     media_name(strm->type), bundle_state_name(st));

	previous = bundle_state(strm->bundle);
	bundle_set_state(strm->bundle, st);

	err = stream_native_mnat_prepare(strm, st != BUNDLE_MUX);
	if (!err) {
		stream_native_mnat_activate(strm);
		stream_native_mnat_finalize(strm);
	}
	else if (err == ENOTSUP && st == BUNDLE_MUX) {
		/* Modules without the transaction hooks retain the legacy behavior. */
		if (strm->mnat)
			disable_mnat(strm);
		if (strm->menc && !strm->menc_transport)
			disable_menc(strm);
	}
	else if (err != EALREADY) {
		bundle_set_state(strm->bundle, previous);
		return;
	}

	bundle_start_socket(strm->bundle, rtp_sock(strm->rtp), strm->le.list);
}


void stream_stage_bundle(struct stream *strm, enum bundle_state st)
{
	if (strm)
		bundle_set_state(strm->bundle, st);
}


int stream_prepare_bundle(struct stream *strm)
{
	enum bundle_state state;
	int err;

	if (!strm)
		return EINVAL;
	state = bundle_state(strm->bundle);
	err = stream_native_mnat_prepare(strm, state != BUNDLE_MUX);
	if (err)
		return err;
	if (state != BUNDLE_NONE) {
		err = bundle_start_socket(strm->bundle, rtp_sock(strm->rtp),
					  strm->le.list);
		if (err && err != EALREADY) {
			stream_native_mnat_abort(strm);
			return err;
		}
	}
	return 0;
}


void stream_commit_bundle(struct stream *strm)
{
	if (!strm)
		return;
	stream_native_mnat_activate(strm);
}


void stream_finalize_bundle(struct stream *strm)
{
	stream_native_mnat_finalize(strm);
}


void stream_rollback_bundle(struct stream *strm)
{
	stream_native_mnat_rollback(strm);
}


void stream_abort_bundle(struct stream *strm)
{
	stream_native_mnat_abort(strm);
}


void stream_enable_natpinhole(struct stream *strm, bool enable)
{
	if (!strm)
		return;

	strm->pinhole = enable;
}
