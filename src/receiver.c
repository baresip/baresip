/**
 * @file receiver.c  Generic stream receiver
 *
 * Copyright (C) 2023 Alfred E. Heggestad, Christian Spielberger
 */
#include <string.h>
#include <time.h>
#include <re.h>
#include <baresip.h>
#include "core.h"


/* Receive */
struct receiver {
	/* Data protected by mtx */
	enum media_type type;          /**< Media type                       */
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
	mtx_t *mtx;                    /**< Mutex protects above fields      */

	/* Unprotected data */
	struct stream *strm;           /**< Stream                           */
	stream_pt_h *pth;              /**< Stream payload type handler      */
	stream_rtp_h *rtph;            /**< Stream RTP handler               */
	stream_rtpestab_h *rtpestabh;  /**< RTP established handler          */
	void *arg;                     /**< Stream argument                  */
	void *sessarg;                 /**< Session argument                 */
};


static void destructor(void *arg)
{
	struct receiver *rx = arg;

	mem_deref(rx->metric);
	mem_deref(rx->name);
	mem_deref(rx->mtx);
	mem_deref(rx->jbuf);
}


int rx_alloc(struct receiver **rxp,
	    const char *name,
	    enum media_type type,
	    const struct config_avt *cfg,
	    stream_rtp_h *rtph,
	    stream_pt_h *pth, void *arg)
{
	struct receiver *rx;
	int err;

	if (!rxp || !str_isset(name))
		return EINVAL;

	rx = mem_zalloc(sizeof(*rx), destructor);
	if (!rx)
		return ENOMEM;

	rx->type  = type;
	rx->rtph  = rtph;
	rx->pth   = pth;
	rx->arg   = arg;
	rx->pseq  = -1;
	err  = str_dup(&rx->name, name);
	err |= mutex_alloc(&rx->mtx);

	/* Audio Jitter buffer */
	if (type == MEDIA_AUDIO &&
	    cfg->audio.jbtype != JBUF_OFF && cfg->audio.jbuf_del.max) {

		err = jbuf_alloc(&rx->jbuf, cfg->audio.jbuf_del.min,
				 cfg->audio.jbuf_del.max);
		err |= jbuf_set_type(rx->jbuf, cfg->audio.jbtype);
	}

	/* Video Jitter buffer */
	if (type == MEDIA_VIDEO &&
	    cfg->video.jbtype != JBUF_OFF && cfg->video.jbuf_del.max) {

		err = jbuf_alloc(&rx->jbuf, cfg->video.jbuf_del.min,
				 cfg->video.jbuf_del.max);
		err |= jbuf_set_type(rx->jbuf, cfg->video.jbtype);
	}

	rx->metric = metric_alloc();
	if (!rx->metric)
		err |= ENOMEM;
	else
		err |= metric_init(rx->metric);

	if (err)
		mem_deref(rx);
	else
		*rxp = rx;

	return err;
}


void rx_set_handlers(struct receiver *rx, struct stream *strm,
		     stream_rtpestab_h *rtpestabh, void *arg)
{
	if (!rx)
		return;

	mtx_lock(rx->mtx);
	rx->strm          = strm;
	rx->rtpestabh     = rtpestabh;
	rx->sessarg       = arg;
	mtx_unlock(rx->mtx);
}


struct metric *rx_metric(struct receiver *rx)
{
	/* it is allowed to return metric because it is thread safe */
	return rx->metric;
}


static int lostcalc(struct receiver *rx, uint16_t seq)
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


static int handle_rtp(struct receiver *rx, const struct rtp_header *hdr,
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
			debug("stream: unknown ext type ignored (0x%04x)\n",
			     hdr->x.type);
			goto handler;
		}

		size_t ext_len = hdr->x.len*sizeof(uint32_t);
		if (mb->pos < ext_len) {
			warning("stream: corrupt rtp packet,"
				" not enough space for rtpext of %zu bytes\n",
				ext_len);
			return 0;
		}

		mb->pos = mb->pos - ext_len;
		mb->end = ext_stop;

		for (i=0; i<RE_ARRAY_SIZE(extv) && mbuf_get_left(mb); i++) {

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
	stream_stop_natpinhole(rx->strm);

	rx->rtph(hdr, extv, extc, mb, lostc, &ignore, rx->arg);
	if (ignore)
		return EAGAIN;

	return 0;
}


/**
 * Decodes one RTP packet
 *
 * @param s The stream
 *
 * @return 0 if success, EAGAIN if it should be called again in order to avoid
 * a jitter buffer overflow, otherwise errorcode
 */
static int decode_frame(struct receiver *rx)
{
	struct rtp_header hdr;
	void *mb;
	int lostc;
	int err;
	int err2;

	if (!rx)
		return EINVAL;

	if (!rx->jbuf)
		return ENOENT;

	err = jbuf_get(rx->jbuf, &hdr, &mb);
	if (err && err != EAGAIN)
		return ENOENT;

	lostc = lostcalc(rx, hdr.seq);

	err2 = handle_rtp(rx, &hdr, mb, lostc > 0 ? lostc : 0, err == EAGAIN);
	mem_deref(mb);

	if (err2 == EAGAIN)
		return err2;

	return err;
}


int rx_receive(struct receiver *rx, const struct sa *src,
		const struct rtp_header *hdr, struct mbuf *mb)
{
	uint32_t ssrc0;
	bool flush = false;
	bool first = false;
	int err = 0;

	if (!rx)
		return EINVAL;

	mtx_lock(rx->mtx);
	if (!rx->enabled && rx->type == MEDIA_AUDIO)
		goto unlock;

	if (rtp_pt_is_rtcp(hdr->pt)) {
		debug("stream: drop incoming RTCP packet on RTP port"
		     " (pt=%u)\n", hdr->pt);
		err = ENOENT;
		goto unlock;
	}

	rx->ts_last = tmr_jiffies();

	metric_add_packet(rx->metric, mbuf_get_left(mb));

	if (!rx->rtp_estab) {
		debug("stream: incoming rtp for '%s' established, "
		     "receiving from %J\n", rx->name, src);
		rx->rtp_estab = true;

		/*TODO: pass to main thread */
		if (rx->rtpestabh)
			rx->rtpestabh(rx->strm, rx->sessarg);
	}

	ssrc0 = rx->ssrc;
	if (!rx->pseq_set) {
		rx->ssrc = hdr->ssrc;
		rx->ssrc_set = true;
		rx->pseq = hdr->seq - 1;
		rx->pseq_set = true;
		first = true;
	}
	else if (hdr->ssrc != ssrc0) {

		debug("stream: %s: SSRC changed 0x%x -> 0x%x"
		     " (%u bytes from %J)\n",
		     rx->name, ssrc0, hdr->ssrc,
		     mbuf_get_left(mb), src);

		rx->ssrc = hdr->ssrc;
		rx->pseq = hdr->seq - 1;
		flush = true;
	}
	mtx_unlock(rx->mtx);

	/* payload-type changed? */
	/*TODO: pass to main thread */
	err = rx->pth(hdr->pt, mb, rx->arg);
	if (err && err != ENODATA)
		goto out;

	if (rx->jbuf) {

		/* Put frame in Jitter Buffer */
		if (flush)
			jbuf_flush(rx->jbuf);

		if (first && err == ENODATA)
			goto out;

		err = jbuf_put(rx->jbuf, hdr, mb);
		if (err) {
			info("stream: %s: dropping %u bytes from %J"
			     " [seq=%u, ts=%u] (%m)\n",
			     rx->name, mb->end,
			     src, hdr->seq, hdr->ts, err);
			metric_inc_err(rx->metric);
		}

		uint32_t n = jbuf_packets(rx->jbuf);
		while (n--) {
			if (decode_frame(rx) != EAGAIN)
				break;
		}
	}
	else {
		(void)handle_rtp(rx, hdr, mb, 0, false);
	}

out:
	return err;

unlock:
	mtx_unlock(rx->mtx);
	return err;
}


void rx_handle_rtcp(struct receiver *rx, struct rtcp_msg *msg)
{
	if (!rx)
		return;

	mtx_lock(rx->mtx);
	rx->ts_last = tmr_jiffies();
	mtx_unlock(rx->mtx);

	/*TODO: process the rest in main thread */
	stream_process_rtcp(rx->strm, msg);
}


void rx_set_ssrc(struct receiver *rx, uint32_t ssrc)
{
	mtx_lock(rx->mtx);
	if (rx->ssrc_set) {
		if (ssrc != rx->ssrc) {
			debug("stream: receive: SSRC changed: %x -> %x\n",
			     rx->ssrc, ssrc);
			rx->ssrc = ssrc;
		}
	}
	else {
		debug("stream: receive: setting SSRC: %x\n", ssrc);
		rx->ssrc = ssrc;
		rx->ssrc_set = true;
	}
	mtx_unlock(rx->mtx);
}


uint64_t rx_ts_last(struct receiver *rx)
{
	uint64_t ts_last;
	mtx_lock(rx->mtx);
	ts_last = rx->ts_last;
	mtx_unlock(rx->mtx);

	return ts_last;
}


void rx_set_ts_last(struct receiver *rx, uint64_t ts_last)
{
	mtx_lock(rx->mtx);
	rx->ts_last = ts_last;
	mtx_unlock(rx->mtx);
}


void rx_flush(struct receiver *rx)
{
	if (!rx)
		return;

	jbuf_flush(rx->jbuf);
}


void rx_set_enable(struct receiver *rx, bool enable)
{
	if (!rx)
		return;

	mtx_lock(rx->mtx);
	rx->enabled = enable;
	mtx_unlock(rx->mtx);
}


int rx_get_ssrc(struct receiver *rx, uint32_t *ssrc)
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


/**
 * The debug function prints into the given mbuf in order to avoid long
 * blocking print to stdout.
 *
 * @param rx The receiver
 * @param mb Memory buffer
 *
 * @return 0 if success, otherwise errorcode
 */
int rx_debug(struct re_printf *pf, const struct receiver *rx)
{
	int err;
	bool enabled;

	mtx_lock(rx->mtx);
	enabled = rx->enabled;
	mtx_unlock(rx->mtx);

	err  = re_hprintf(pf, " rx.enabled: %s\n", enabled ? "yes" : "no");
	err |= jbuf_debug(pf, rx->jbuf);

	return err;
}
