/**
 * @file rtprecv.c  Generic RTP stream receiver
 *
 * Copyright (C) 2023 Alfred E. Heggestad, Christian Spielberger
 */
#include <string.h>
#include <time.h>
#include <re_atomic.h>
#include <re.h>
#include <baresip.h>
#include "core.h"

/** Magic number */
#define MAGIC 0x00511eb3
#include "magic.h"

/* Receive */
struct rtp_receiver {
#ifndef RELEASE
	uint32_t magic;                /**< Magic number for debugging       */
#endif
	/* Data protected by mtx */
	char *name;                    /**< Media name                       */
	struct metric *metric;         /**< Metrics for receiving            */
	struct jbuf *jbuf;             /**< Jitter Buffer for incoming RTP   */
	bool enabled;                  /**< True if enabled                  */
	uint64_t ts_last;              /**< Timestamp of last recv RTP pkt   */
	uint32_t ssrc;                 /**< Incoming synchronization source  */
	bool ssrc_set;                 /**< Incoming SSRC is set             */
	uint32_t pseq;                 /**< Sequence number for incoming RTP */
	bool pseq_set;                 /**< True if sequence number is set   */
	bool rtp_estab;                /**< True if RTP stream established   */
	RE_ATOMIC bool run;            /**< True if RX thread is running     */
	bool start_rtcp;               /**< Start RTCP flag                  */
	char *cname;                   /**< Canonical Name for RTCP send     */
	struct sa rtcp_peer;           /**< RTCP address of Peer             */
	bool pinhole;                  /**< Open RTCP NAT pinhole flag       */
	mtx_t *mtx;                    /**< Mutex protects above fields      */

	/* Unprotected data */
	struct stream *strm;           /**< Stream                           */
	struct rtp_sock *rtp;          /**< RTP Socket                       */
	stream_pt_h *pth;              /**< Stream payload type handler      */
	stream_rtp_h *rtph;            /**< Stream RTP handler               */
	stream_rtpestab_h *rtpestabh;  /**< RTP established handler          */
	void *arg;                     /**< Stream argument                  */
	void *sessarg;                 /**< Session argument                 */
	thrd_t thr;                    /**< RX thread                        */
	struct tmr tmr;                /**< Timer for stopping RX thread     */
	int pt;                        /**< Previous payload type            */
	int pt_tel;                    /**< Payload type for tel event       */
	uint32_t srate;                /**< Receiver Samplerate              */
	struct tmr tmr_decode;         /**< Decode Timer                     */
};


enum work_type {
	WORK_RTCP,
	WORK_RTPESTAB,
	WORK_PTCHANGED,
	WORK_MNATCONNH,
};


struct work {
	enum work_type type;
	struct rtp_receiver *rx;
	union {
		struct rtcp_msg *rtcp;
		struct {
			uint8_t pt;
			struct mbuf *mb;
		} pt;
		struct {
			struct sa raddr1;
			struct sa raddr2;
		} mnat;
	} u;
};


static void async_work_main(int err, void *arg);
static void work_destructor(void *arg);


/*
 * functions that run in RX thread (if "rxmode thread" is configured)
 */


static void pass_rtcp_work(struct rtp_receiver *rx, struct rtcp_msg *msg)
{
	struct work *w;

	if (!re_atomic_rlx(&rx->run)) {
		stream_process_rtcp(rx->strm, msg);
		return;
	}

	w = mem_zalloc(sizeof(*w), work_destructor);
	if (!w)
		return;

	w->type    = WORK_RTCP;
	w->rx      = rx;
	w->u.rtcp  = mem_ref(msg);
	re_thread_async_main_id((intptr_t)rx, NULL, async_work_main, w);
}


static int pass_pt_work(struct rtp_receiver *rx, uint8_t pt, struct mbuf *mb)
{
	struct work *w;

	if (!re_atomic_rlx(&rx->run))
		return rx->pth(pt, mb, rx->arg);

	w = mem_zalloc(sizeof(*w), work_destructor);
	if (!w)
		return ENOMEM;

	w->type    = WORK_PTCHANGED;
	w->rx      = rx;
	w->u.pt.pt = pt;
	w->u.pt.mb = mbuf_dup(mb);

	return re_thread_async_main_id((intptr_t)rx, NULL, async_work_main, w);
}


static void pass_rtpestab_work(struct rtp_receiver *rx)
{
	struct work *w;

	if (!re_atomic_rlx(&rx->run)) {
		rx->rtpestabh(rx->strm, rx->sessarg);
		return;
	}

	w = mem_zalloc(sizeof(*w), work_destructor);
	w->type = WORK_RTPESTAB;
	w->rx   = rx;

	re_thread_async_main_id((intptr_t)rx, NULL, async_work_main, w);
}


static void pass_mnat_work(struct rtp_receiver *rx, const struct sa *raddr1,
			   const struct sa *raddr2)
{
	struct work *w;

	if (!re_atomic_rlx(&rx->run)) {
		stream_mnat_connected(rx->strm, raddr1, raddr2);
		return;
	}

	w = mem_zalloc(sizeof(*w), work_destructor);
	w->type = WORK_MNATCONNH;
	w->rx   = rx;
	sa_cpy(&w->u.mnat.raddr1, raddr1);
	sa_cpy(&w->u.mnat.raddr2, raddr2);

	re_thread_async_main_id((intptr_t)rx, NULL, async_work_main, w);
}


static void rtprecv_periodic(void *arg)
{
	struct rtp_receiver *rx = arg;

	if (re_atomic_rlx(&rx->run)) {
		mtx_lock(rx->mtx);
		bool pinhole    = rx->pinhole;
		mtx_unlock(rx->mtx);
		tmr_start(&rx->tmr, 10, rtprecv_periodic, rx);
		mtx_lock(rx->mtx);
		if (rx->start_rtcp) {
			int err = 0;
			rx->start_rtcp = false;
			rtcp_start(rx->rtp, rx->cname, &rx->rtcp_peer);
			mtx_unlock(rx->mtx);
			if (pinhole) {
				err = rtcp_send_app(rx->rtp, "PING",
						    (void *)"PONG", 4);
			}
			if (err) {
				warning("rtprecv: rtcp_send_app failed (%m)\n",
					err);
			}
		}
		else {
			mtx_unlock(rx->mtx);
		}
	}
	else {
		udp_thread_detach(rtp_sock(rx->rtp));
		udp_thread_detach(rtcp_sock(rx->rtp));
		re_cancel();
	}
}


static int rtprecv_thread(void *arg)
{
	struct rtp_receiver *rx = arg;
	int err;

	re_thread_init();
	info("rtp_receiver: RTP RX thread started\n");
	tmr_start(&rx->tmr, 10, rtprecv_periodic, rx);

	err = udp_thread_attach(rtp_sock(rx->rtp));
	if (err) {
		warning("rtp_receiver: could not attach to RTP socket (%m)\n",
			err);
		return err;
	}

	err = udp_thread_attach(rtcp_sock(rx->rtp));
	if (err) {
		warning("rtp_receiver: could not attach to RTCP socket (%m)\n",
			err);
		return err;
	}

	err = re_main(NULL);

	tmr_cancel(&rx->tmr);
	re_thread_close();
	return err;
}


static int lostcalc(struct rtp_receiver *rx, uint16_t seq)
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


static int handle_rtp(struct rtp_receiver *rx, const struct rtp_header *hdr,
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
		size_t i;
		int err;

		if (hdr->x.type != RTPEXT_TYPE_MAGIC) {
			debug("rtprecv: unknown ext type ignored (0x%04x)\n",
			     hdr->x.type);
			goto handler;
		}

		size_t ext_len = hdr->x.len*sizeof(uint32_t);
		if (mb->pos < ext_len) {
			warning("rtp_receiver: corrupt rtp packet,"
				" not enough space for rtpext of %zu bytes\n",
				ext_len);
			return 0;
		}

		mb->pos = mb->pos - ext_len;
		mb->end = ext_stop;

		for (i=0; i<RE_ARRAY_SIZE(extv) && mbuf_get_left(mb); i++) {

			err = rtpext_decode(&extv[i], mb);
			if (err) {
				warning("rtp_receiver: rtpext_decode failed "
					"(%m)\n", err);
				return 0;
			}
		}

		extc = i;

		mb->pos = pos;
		mb->end = end;
	}

 handler:
	stream_stop_natpinhole(rx->strm);

	rx->rtph(hdr, extv, extc, mb, lostc, &ignore, rx->arg);
	if (ignore)
		return EAGAIN;

	return 0;
}


static void decode_frames(struct rtp_receiver *rx);
static void decode_tmr(void *arg)
{
	struct rtp_receiver *rx = arg;

	decode_frames(rx);
}


/**
 * Decode RTP packets
 *
 * @param s The stream
 */
static void decode_frames(struct rtp_receiver *rx)
{
	struct rtp_header hdr;
	void *mb;
	int lostc;
	int32_t delay;
	int err;

	if (!rx || !rx->jbuf)
		return;

	uint32_t n = jbuf_packets(rx->jbuf);

	do {
		err = jbuf_get(rx->jbuf, &hdr, &mb);
		if (err && err != EAGAIN)
			break;

		lostc = lostcalc(rx, hdr.seq);

		err = handle_rtp(rx, &hdr, mb, lostc > 0 ? lostc : 0,
				 err == EAGAIN);
		mem_deref(mb);

		if (err && err != EAGAIN)
			break;
	} while (n--);

	delay = jbuf_next_play(rx->jbuf);
	if (delay < 0)
		delay = 10; /* Fallback time */

	tmr_start(&rx->tmr_decode, delay, decode_tmr, rx);
}


static bool rtprecv_filter_pt(struct rtp_receiver *rx,
			      const struct rtp_header *hdr)
{
	bool handle;

	handle = hdr->pt != rx->pt;
	if (rx->pt_tel)
		handle |= hdr->pt == rx->pt_tel;

	if (!handle)
		return false;

	const struct sdp_format *lc;
	lc = sdp_media_lformat(stream_sdpmedia(rx->strm), hdr->pt);
	if (lc && !str_casecmp(lc->name, "telephone-event")) {
		rx->pt_tel = hdr->pt;
	}

	rx->pt = hdr->pt;
	return true;
}


void rtprecv_decode(const struct sa *src, const struct rtp_header *hdr,
		     struct mbuf *mb, void *arg)
{
	struct rtp_receiver *rx = arg;
	uint32_t ssrc0;
	bool flush = false;
	int err = 0;

	if (!rx)
		return;

	MAGIC_CHECK(rx);
	mtx_lock(rx->mtx);
	if (!rx->enabled) {
		mtx_unlock(rx->mtx);
		return;
	}

	if (rtp_pt_is_rtcp(hdr->pt)) {
		debug("rtprecv: drop incoming RTCP packet on RTP port"
		     " (pt=%u)\n", hdr->pt);
		mtx_unlock(rx->mtx);
		return;
	}

	rx->ts_last = tmr_jiffies();

	metric_add_packet(rx->metric, mbuf_get_left(mb));

	if (!rx->rtp_estab) {
		if (rx->rtpestabh) {
			debug("rtprecv: incoming rtp for '%s' established, "
			      "receiving from %J\n", rx->name, src);
			rx->rtp_estab = true;
			pass_rtpestab_work(rx);
		}
		tmr_start(&rx->tmr_decode, 0, decode_tmr, rx);
	}

	ssrc0 = rx->ssrc;
	if (!rx->pseq_set) {
		rx->ssrc = hdr->ssrc;
		rx->ssrc_set = true;
		rx->pseq = hdr->seq - 1;
		rx->pseq_set = true;
	}
	else if (hdr->ssrc != ssrc0) {

		debug("rtprecv: %s: SSRC changed 0x%x -> 0x%x"
		     " (%zu bytes from %J)\n",
		     rx->name, ssrc0, hdr->ssrc,
		     mbuf_get_left(mb), src);

		rx->ssrc = hdr->ssrc;
		rx->pseq = hdr->seq - 1;
		flush = true;
	}
	mtx_unlock(rx->mtx);

	if (rtprecv_filter_pt(rx, hdr)) {
		err = pass_pt_work(rx, hdr->pt, mb);
		if (err)
			return;
	}

	if (rx->jbuf) {
		/* Put frame in Jitter Buffer */
		if (flush)
			jbuf_flush(rx->jbuf);

		err = jbuf_put(rx->jbuf, hdr, mb);
		if (err) {
			info("rtprecv: %s: dropping %zu bytes from %J"
			     " [seq=%u, ts=%u] (%m)\n",
			     rx->name, mb->end,
			     src, hdr->seq, hdr->ts, err);
			metric_inc_err(rx->metric);
		}
	}
	else {
		(void)handle_rtp(rx, hdr, mb, 0, false);
	}
}


void rtprecv_handle_rtcp(const struct sa *src, struct rtcp_msg *msg,
			  void *arg)
{
	struct rtp_receiver *rx = arg;
	(void)src;

	MAGIC_CHECK(rx);
	mtx_lock(rx->mtx);
	if (!rx->enabled) {
		mtx_unlock(rx->mtx);
		return;
	}

	rx->ts_last = tmr_jiffies();
	mtx_unlock(rx->mtx);

	pass_rtcp_work(rx, msg);
}


void rtprecv_mnat_connected_handler(const struct sa *raddr1,
				     const struct sa *raddr2, void *arg)
{
	struct rtp_receiver *rx = arg;

	MAGIC_CHECK(rx);

	pass_mnat_work(rx, raddr1, raddr2);
}


int rtprecv_start_rtcp(struct rtp_receiver *rx, const char *cname,
		       const struct sa *peer, bool pinhole)
{
	int err;

	if (!rx)
		return EINVAL;

	mtx_lock(rx->mtx);
	if (peer)
		rx->rtcp_peer = *peer;


	rx->cname = mem_deref(rx->cname);
	err = str_dup(&rx->cname, cname);
	rx->start_rtcp = true;
	rx->pinhole = pinhole;
	mtx_unlock(rx->mtx);

	return err;
}


/*
 * functions that run in main thread
 */

void rtprecv_set_socket(struct rtp_receiver *rx, struct rtp_sock *rtp)
{
	mtx_lock(rx->mtx);
	rx->rtp = rtp;
	if (stream_type(rx->strm) == MEDIA_VIDEO)
		jbuf_set_gnack(rx->jbuf, rx->rtp);
	mtx_unlock(rx->mtx);
}


void rtprecv_set_ssrc(struct rtp_receiver *rx, uint32_t ssrc)
{
	if (!rx)
		return;

	mtx_lock(rx->mtx);
	if (rx->ssrc_set) {
		if (ssrc != rx->ssrc) {
			debug("rtprecv: receive: SSRC changed: %x -> %x\n",
			     rx->ssrc, ssrc);
			rx->ssrc = ssrc;
		}
	}
	else {
		debug("rtprecv: receive: setting SSRC: %x\n", ssrc);
		rx->ssrc = ssrc;
		rx->ssrc_set = true;
	}
	mtx_unlock(rx->mtx);
}


uint64_t rtprecv_ts_last(struct rtp_receiver *rx)
{
	if (!rx)
		return 0;

	uint64_t ts_last;

	mtx_lock(rx->mtx);
	ts_last = rx->ts_last;
	mtx_unlock(rx->mtx);

	return ts_last;
}


void rtprecv_set_ts_last(struct rtp_receiver *rx, uint64_t ts_last)
{
	if (!rx)
		return;

	mtx_lock(rx->mtx);
	rx->ts_last = ts_last;
	mtx_unlock(rx->mtx);
}


void rtprecv_flush(struct rtp_receiver *rx)
{
	if (!rx)
		return;

	jbuf_flush(rx->jbuf);
}


void rtprecv_enable(struct rtp_receiver *rx, bool enable)
{
	if (!rx)
		return;

	mtx_lock(rx->mtx);
	rx->enabled = enable;
	mtx_unlock(rx->mtx);
}


int rtprecv_get_ssrc(struct rtp_receiver *rx, uint32_t *ssrc)
{
	int err;

	if (!rx || !ssrc)
		return EINVAL;

	mtx_lock(rx->mtx);
	if (rx->ssrc_set) {
		*ssrc = rx->ssrc;
		err = 0;
	}
	else
		err = ENOENT;
	mtx_unlock(rx->mtx);

	return err;
}


struct jbuf *rtprecv_jbuf(struct rtp_receiver *rx)
{
	return rx ? rx->jbuf : NULL;
}


void rtprecv_enable_mux(struct rtp_receiver *rx, bool enable)
{
	if (!rx)
		return;

	mtx_lock(rx->mtx);
	rtcp_enable_mux(rx->rtp, enable);
	mtx_unlock(rx->mtx);
}


/**
 * The debug function prints RTP Receiver state using the print function
 *
 * @param pf Print function
 * @param rx The rtp_receiver
 *
 * @return 0 if success, otherwise errorcode
 */
int rtprecv_debug(struct re_printf *pf, const struct rtp_receiver *rx)
{
	int err;
	bool enabled;

	if (!rx)
		return 0;

	mtx_lock(rx->mtx);
	enabled = rx->enabled;
	mtx_unlock(rx->mtx);

	err  = re_hprintf(pf, " rx.enabled: %s\n", enabled ? "yes" : "no");
	err |= jbuf_debug(pf, rx->jbuf);

	return err;
}


static void destructor(void *arg)
{
	struct rtp_receiver *rx = arg;

	if (re_atomic_rlx(&rx->run)) {
		rtprecv_enable(rx, false);
		re_atomic_rlx_set(&rx->run, false);
		thrd_join(rx->thr, NULL);
		re_thread_async_main_cancel((intptr_t)rx);
	}
	else {
		udp_thread_detach(rtp_sock(rx->rtp));
		udp_thread_detach(rtcp_sock(rx->rtp));
	}

	tmr_cancel(&rx->tmr_decode);
	mem_deref(rx->metric);
	mem_deref(rx->name);
	mem_deref(rx->mtx);
	mem_deref(rx->jbuf);
	mem_deref(rx->cname);
}


int rtprecv_alloc(struct rtp_receiver **rxp,
		   struct stream *strm,
		   const char *name,
		   const struct config_avt *cfg,
		   stream_rtp_h *rtph,
		   stream_pt_h *pth, void *arg)
{
	struct rtp_receiver *rx;
	int err;

	if (!rxp || !str_isset(name))
		return EINVAL;

	rx = mem_zalloc(sizeof(*rx), destructor);
	if (!rx)
		return ENOMEM;

	MAGIC_INIT(rx);
	rx->strm   = strm;
	rx->rtph   = rtph;
	rx->pth    = pth;
	rx->arg    = arg;
	rx->pseq   = -1;
	rx->pt     = -1;

	err  = str_dup(&rx->name, name);
	err |= mutex_alloc(&rx->mtx);
	if (err)
		goto out;

	/* Audio Jitter buffer */
	if (stream_type(strm) == MEDIA_AUDIO &&
	    cfg->audio.jbtype != JBUF_OFF && cfg->audio.jbuf_del.max) {

		err = jbuf_alloc(&rx->jbuf, cfg->audio.jbuf_del.min,
				 cfg->audio.jbuf_del.max, cfg->audio.jbuf_sz);
		if (err)
			goto out;

		err = jbuf_set_type(rx->jbuf, cfg->audio.jbtype);
		if (err)
			goto out;
	}

	/* Video Jitter buffer */
	if (stream_type(strm) == MEDIA_VIDEO &&
	    cfg->video.jbtype != JBUF_OFF && cfg->video.jbuf_del.max) {

		err = jbuf_alloc(&rx->jbuf, cfg->video.jbuf_del.min,
				 cfg->video.jbuf_del.max, cfg->video.jbuf_sz);
		if (err)
			goto out;

		err = jbuf_set_type(rx->jbuf, cfg->video.jbtype);
		if (err)
			goto out;
	}

	struct pl *id = pl_alloc_str(name);
	if (!id)
		goto out;

	jbuf_set_id(rx->jbuf, id);
	mem_deref(id);

	rx->metric = metric_alloc();
	if (!rx->metric)
		err = ENOMEM;
	else
		err = metric_init(rx->metric);

out:
	if (err)
		mem_deref(rx);
	else
		*rxp = rx;

	return err;
}


int rtprecv_start_thread(struct rtp_receiver *rx)
{
	int err;

	if (!rx)
		return EINVAL;

	if (re_atomic_rlx(&rx->run))
		return 0;

	udp_thread_detach(rtp_sock(rx->rtp));
	udp_thread_detach(rtcp_sock(rx->rtp));
	re_atomic_rlx_set(&rx->run, true);
	err = thread_create_name(&rx->thr,
				 "RX thread",
				 rtprecv_thread, rx);
	if (err) {
		re_atomic_rlx_set(&rx->run, false);
		udp_thread_attach(rtp_sock(rx->rtp));
		udp_thread_attach(rtcp_sock(rx->rtp));
	}

	return err;
}


bool rtprecv_running(const struct rtp_receiver *rx)
{
	if (!rx)
		return false;

	return re_atomic_rlx(&rx->run);
}


void rtprecv_set_handlers(struct rtp_receiver *rx,
			   stream_rtpestab_h *rtpestabh, void *arg)
{
	if (!rx)
		return;

	mtx_lock(rx->mtx);
	rx->rtpestabh     = rtpestabh;
	rx->sessarg       = arg;
	mtx_unlock(rx->mtx);
}


struct metric *rtprecv_metric(struct rtp_receiver *rx)
{
	if (!rx)
		return NULL;

	/* it is allowed to return metric because it is thread safe */
	return rx->metric;
}


static void work_destructor(void *arg)
{
	struct work *w = arg;

	switch (w->type) {
		case WORK_RTCP:
			mem_deref(w->u.rtcp);
			break;
		case WORK_PTCHANGED:
			mem_deref(w->u.pt.mb);
			break;
		default:
			break;
	}
}


static void async_work_main(int err, void *arg)
{
	struct work *w = arg;
	struct rtp_receiver *rx = w->rx;
	(void)err;

	switch (w->type) {
		case WORK_RTCP:
			stream_process_rtcp(rx->strm, w->u.rtcp);
			break;
		case WORK_PTCHANGED:
			rx->pth(w->u.pt.pt, w->u.pt.mb, rx->arg);
			break;
		case WORK_RTPESTAB:
			rx->rtpestabh(rx->strm, rx->sessarg);
			break;
		case WORK_MNATCONNH:
			stream_mnat_connected(rx->strm,
					      &w->u.mnat.raddr1,
					      &w->u.mnat.raddr2);
			break;
		default:
			break;
	}

	mem_deref(w);
}


void rtprecv_set_srate(struct rtp_receiver *rx, uint32_t srate)
{
	if (!rx)
		return;

	rx->srate = srate;
	jbuf_set_srate(rx->jbuf, srate);
}
