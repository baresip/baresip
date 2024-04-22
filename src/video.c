/**
 * @file src/video.c  Video stream
 *
 * Copyright (C) 2010 Alfred E. Heggestad
 * Copyright (C) 2021 by:
 *     Media Magic Technologies <developer@mediamagictechnologies.com>
 *     and Divus GmbH <developer@divus.eu>
 *
 * \ref GenericVideoStream
 */
#include <string.h>
#include <stdlib.h>
#include <re_atomic.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "core.h"


/** Magic number */
#define MAGIC 0x00070d10
#include "magic.h"


/** Video transmit parameters */
enum {
	MEDIA_POLL_RATE = 250,		       /**< in [Hz]                  */
	RTP_PRESZ	= 4 + RTP_HEADER_SIZE, /**< TURN and RTP header      */
	RTP_TRAILSZ	= 12 + 4,	       /**< SRTP/SRTCP trailer       */
	PICUP_INTERVAL	= 500,		       /**< FIR/PLI interval         */
	NACK_BLPSZ	= 16,		       /**< NACK bitmask size        */
	NACK_QUEUE_TIME	= 500,		       /**< in [ms]                  */
	PKT_SIZE	= 1280,		       /**< max. Packet size in bytes*/
};


/**
 * \page GenericVideoStream Generic Video Stream
 *
 * Implements a generic video stream. The application can allocate multiple
 * instances of a video stream, mapping it to a particular SDP media line.
 * The video object has a Video Display and Source, and a video encoder
 * and decoder. A particular video object is mapped to a generic media
 * stream object.
 *
 *<pre>
 *            recv  send
 *              |    /|\
 *             \|/    |
 *            .---------.    .-------.
 *            |  video  |--->|encoder|
 *            |         |    |-------|
 *            | object  |--->|decoder|
 *            '---------'    '-------'
 *              |    /|\
 *              |     |
 *             \|/    |
 *        .-------.  .-------.
 *        |Video  |  |Video  |
 *        |Display|  |Source |
 *        '-------'  '-------'
 *</pre>
 */

/**
 * Video stream - transmitter/encoder direction

 \verbatim

 Processing encoder pipeline:

 .         .--------.   .- - - - -.   .---------.   .---------.
 | ._O_.   |        |   !         !   |         |   |         |
 | |___|-->| vidsrc |-->! vidconv !-->| vidfilt |-->| encoder |---> RTP
 |         |        |   !         !   |         |   |         |
 '         '--------'   '- - - - -'   '---------'   '---------'
                         (optional)
 \endverbatim
 */
struct vtx {
	struct video *video;               /**< Parent                    */
	const struct vidcodec *vc;         /**< Current Video encoder     */
	struct videnc_state *enc;          /**< Video encoder state       */
	struct vidsrc_prm vsrc_prm;        /**< Video source parameters   */
	struct vidsz vsrc_size;            /**< Video source size         */
	struct vidsrc *vs;                 /**< Video source module       */
	struct vidsrc_st *vsrc;            /**< Video source              */
	mtx_t *lock_enc;                   /**< Lock for encoder          */
	struct vidframe *frame;            /**< Source frame              */
	mtx_t *lock_tx;                    /**< Protect the sendq         */
	struct list sendq;                 /**< Tx-Queue (struct vidqent) */
	struct list sendqnb;               /**< Tx-Queue NACK wait buffer */
	unsigned skipc;                    /**< Number of frames skipped  */
	struct list filtl;                 /**< Filters in encoding order */
	enum vidfmt fmt;                   /**< Outgoing pixel format     */
	char device[128];                  /**< Source device name        */
	uint32_t ts_offset;                /**< Random timestamp offset   */
	bool picup;                        /**< Send picture update       */
	int frames;                        /**< Number of frames sent     */
	double efps;                       /**< Estimated frame-rate      */
	uint64_t ts_base;                  /**< First RTP timestamp sent  */
	uint64_t ts_last;                  /**< Last RTP timestamp sent   */
	thrd_t thrd;                       /**< Tx-Thread                 */
	RE_ATOMIC bool run;                /**< Tx-Thread is active       */
	cnd_t wait;                        /**< Tx-Thread wait            */

	/** Statistics */
	struct {
		uint64_t src_frames;       /**< Total frames from vidsrc  */
	} stats;
};


/**
 * Video stream - receiver/decoder direction

 \verbatim

 Processing decoder pipeline:

 .~~~~~~~~.   .--------.   .---------.   .---------.
 |  _o_   |   |        |   |         |   |         |
 |   |    |<--| vidisp |<--| vidfilt |<--| decoder |<--- RTP
 |  /'\   |   |        |   |         |   |         |
 '~~~~~~~~'   '--------'   '---------'   '---------'

 \endverbatim

 */
struct vrx {
	struct video *video;               /**< Parent                    */
	const struct vidcodec *vc;         /**< Current video decoder     */
	struct viddec_state *dec;          /**< Video decoder state       */
	struct vidisp_prm vidisp_prm;      /**< Video display parameters  */
	struct vidisp *vd;                 /**< Video display module      */
	struct vidisp_st *vidisp;          /**< Video display             */
	mtx_t lock;                        /**< Lock for decoder          */
	struct list filtl;                 /**< Filters in decoding order */
	struct tmr tmr_picup;              /**< Picture update timer      */
	struct vidsz size;                 /**< Incoming video resolution */
	enum vidfmt fmt;                   /**< Incoming pixel format     */
	enum vidorient orient;             /**< Display orientation       */
	char device[128];                  /**< Display device name       */
	int pt_rx;                         /**< Incoming RTP payload type */
	int frames;                        /**< Number of frames received */
	double efps;                       /**< Estimated frame-rate      */
	unsigned n_intra;                  /**< Intra-frames decoded      */
	unsigned n_picup;                  /**< Picture updates sent      */
	struct timestamp_recv ts_recv;     /**< Receive timestamp state   */

	/** Statistics */
	struct {
		uint64_t disp_frames;      /** Total frames displayed     */
	} stats;
};


/** Generic Video stream */
struct video {
	MAGIC_DECL              /**< Magic number for debugging           */
	struct config_video cfg;/**< Video configuration                  */
	struct stream *strm;    /**< Generic media stream                 */
	struct vtx vtx;         /**< Transmit/encoder direction           */
	struct vrx vrx;         /**< Receive/decoder direction            */
	struct tmr tmr;         /**< Timer for frame-rate estimation      */
	char *peer;             /**< Peer URI                             */
	bool nack_pli;          /**< Send NACK/PLI to peer                */
	video_err_h *errh;      /**< Error handler                        */
	void *arg;              /**< Error handler argument               */
};


struct vidqent {
	struct le le;
	bool ext;
	bool marker;
	uint8_t pt;
	uint32_t ts;
	uint64_t jfs_nack;
	uint16_t seq;
	struct mbuf *mb;
};


static void request_picture_update(struct vrx *vrx);
static void video_stop_source(struct video *v);


static void vidqent_destructor(void *arg)
{
	struct vidqent *qent = arg;

	list_unlink(&qent->le);
	mem_deref(qent->mb);
}


static int vidqent_alloc(struct vidqent **qentp, struct stream *strm,
			 bool marker, uint8_t pt, uint32_t ts,
			 const uint8_t *hdr, size_t hdr_len,
			 const uint8_t *pld, size_t pld_len)
{
	struct bundle *bun = stream_bundle(strm);
	struct vidqent *qent;
	int err = 0;

	if (!qentp || !pld)
		return EINVAL;

	qent = mem_zalloc(sizeof(*qent), vidqent_destructor);
	if (!qent)
		return ENOMEM;

	qent->ext    = false;
	qent->marker = marker;
	qent->pt     = pt;
	qent->ts     = ts;

	qent->mb = mbuf_alloc(RTP_PRESZ + hdr_len + pld_len + RTP_TRAILSZ);
	if (!qent->mb) {
		err = ENOMEM;
		goto out;
	}

	qent->mb->pos = qent->mb->end = RTP_PRESZ;

	if (bundle_state(bun) != BUNDLE_NONE) {

		const char *mid = stream_mid(strm);
		size_t ext_len = 0;
		size_t start = qent->mb->pos;
		size_t pos;

		/* skip the extension header */
		qent->mb->pos = start + RTPEXT_HDR_SIZE;

		pos = qent->mb->pos;

		rtpext_encode(qent->mb, bundle_extmap_mid(bun),
			      str_len(mid), (void *)mid);

		ext_len = qent->mb->pos - pos;

		/* write the Extension header at the beginning */
		qent->mb->pos = start;

		err = rtpext_hdr_encode(qent->mb, ext_len);
		if (err)
			goto out;

		qent->mb->pos = start + RTPEXT_HDR_SIZE + ext_len;
		qent->mb->end = start + RTPEXT_HDR_SIZE + ext_len;

		qent->ext = true;
	}

	if (hdr)
		(void)mbuf_write_mem(qent->mb, hdr, hdr_len);

	(void)mbuf_write_mem(qent->mb, pld, pld_len);

	qent->mb->pos = RTP_PRESZ;

 out:
	if (err)
		mem_deref(qent);
	else
		*qentp = qent;

	return err;
}


static void video_destructor(void *arg)
{
	struct video *v = arg;
	struct vtx *vtx = &v->vtx;
	struct vrx *vrx = &v->vrx;

	stream_enable(v->strm, false);

	/* transmit */
	if (re_atomic_rlx(&vtx->run)) {
		re_atomic_rlx_set(&vtx->run, false);
		cnd_signal(&vtx->wait);
		thrd_join(vtx->thrd, NULL);
	}
	mtx_lock(vtx->lock_tx);
	list_flush(&vtx->sendq);
	list_flush(&vtx->sendqnb);
	mtx_unlock(vtx->lock_tx);
	mem_deref(vtx->lock_tx);

	mem_deref(vtx->vsrc);
	mtx_lock(vtx->lock_enc);
	mem_deref(vtx->frame);
	mem_deref(vtx->enc);
	list_flush(&vtx->filtl);
	mtx_unlock(vtx->lock_enc);
	mem_deref(vtx->lock_enc);

	/* receive */
	tmr_cancel(&vrx->tmr_picup);
	mtx_lock(&vrx->lock);
	mem_deref(vrx->dec);
	mem_deref(vrx->vidisp);
	list_flush(&vrx->filtl);
	mtx_unlock(&vrx->lock);
	mtx_destroy(&vrx->lock);

	tmr_cancel(&v->tmr);
	mem_deref(v->strm);
	mem_deref(v->peer);
}


static double get_fps(const struct video *v)
{
	const char *attr;

	/* RFC4566 */
	attr = sdp_media_rattr(stream_sdpmedia(v->strm), "framerate");
	if (attr) {
		return atof(attr);
	}
	else
		return v->cfg.fps;
}


static int packet_handler(bool marker, uint64_t ts,
			  const uint8_t *hdr, size_t hdr_len,
			  const uint8_t *pld, size_t pld_len,
			  const struct video *vid)
{
	struct vtx *vtx = (struct vtx *)&vid->vtx;
	struct stream *strm = vid->strm;
	struct vidqent *qent;
	uint32_t rtp_ts;
	int pt;
	int err;

	MAGIC_CHECK(vid);

	mtx_lock(vtx->lock_tx);
	if (!vtx->ts_base)
		vtx->ts_base = ts;
	vtx->ts_last = ts;
	pt = stream_pt_enc(strm);
	mtx_unlock(vtx->lock_tx);

	/* add random timestamp offset */
	rtp_ts = vtx->ts_offset + (ts & 0xffffffff);

	err = vidqent_alloc(&qent, strm, marker, pt, rtp_ts,
			    hdr, hdr_len, pld, pld_len);
	if (err)
		return err;

	mtx_lock(vtx->lock_tx);
	list_append(&vtx->sendq, &qent->le, qent);
	mtx_unlock(vtx->lock_tx);

	cnd_signal(&vtx->wait);

	return 0;
}


/**
 * Encode video and send via RTP stream
 *
 * @note This function has REAL-TIME properties
 *
 * @param vtx        Video transmit object
 * @param frame      Video frame to send
 * @param timestamp  Frame timestamp in VIDEO_TIMEBASE units
 */
static void encode_rtp_send(struct vtx *vtx, struct vidframe *frame,
			    struct vidpacket *packet, uint64_t timestamp)
{
	struct le *le;
	int err = 0;
	bool sendq_empty;

	if (!vtx->enc)
		return;

	if (packet) {
		mtx_lock(vtx->lock_enc);

		if (vtx->vc && vtx->vc->packetizeh) {
			err = vtx->vc->packetizeh(vtx->enc, packet);
			if (err)
				goto out;

			vtx->picup = false;
		}
		else {
			warning("video: Skipping Packet as"
				" Packetize Handler not initialized ..\n");
		}
		goto out;
	}

	mtx_lock(vtx->lock_tx);
	sendq_empty = (vtx->sendq.head == NULL);

	if (!sendq_empty) {
		++vtx->skipc;
		mtx_unlock(vtx->lock_tx);
		return;
	}
	mtx_unlock(vtx->lock_tx);

	mtx_lock(vtx->lock_enc);

	/* Convert image */
	if (frame->fmt != (enum vidfmt)vtx->video->cfg.enc_fmt) {

		vtx->vsrc_size = frame->size;

		if (!vtx->frame) {

			err = vidframe_alloc(&vtx->frame,
					     vtx->video->cfg.enc_fmt,
					     &vtx->vsrc_size);
			if (err)
				goto out;
		}

		vidconv(vtx->frame, frame, 0);
		frame = vtx->frame;
	}

	/* Process video frame through all Video Filters */
	for (le = vtx->filtl.head; le; le = le->next) {

		struct vidfilt_enc_st *st = le->data;

		if (st->vf && st->vf->ench)
			err |= st->vf->ench(st, frame, &timestamp);
	}

	if (err)
		goto out;

	if (frame)
		vtx->fmt = frame->fmt;

	/* Encode the whole picture frame */
	err = vtx->vc->ench(vtx->enc, vtx->picup, frame, timestamp);
	if (err)
		goto out;

	vtx->picup = false;

 out:
	mtx_unlock(vtx->lock_enc);
}


/**
 * Read frames from video source
 *
 * @param frame      Video frame
 * @param timestamp  Frame timestamp in VIDEO_TIMEBASE units
 * @param arg        Handler argument
 *
 * @note This function has REAL-TIME properties
 */
static void vidsrc_frame_handler(struct vidframe *frame, uint64_t timestamp,
				 void *arg)
{
	struct vtx *vtx = arg;

	MAGIC_CHECK(vtx->video);

	mtx_lock(vtx->lock_enc);
	++vtx->frames;
	++vtx->stats.src_frames;
	mtx_unlock(vtx->lock_enc);

	/* Encode and send */
	encode_rtp_send(vtx, frame, NULL, timestamp);
}


static void vidsrc_packet_handler(struct vidpacket *packet, void *arg)
{
	struct vtx *vtx = arg;

	MAGIC_CHECK(vtx->video);

	/* Encode and send */
	encode_rtp_send(vtx, NULL, packet, packet->timestamp);
}


static void vidsrc_error_handler(int err, void *arg)
{
	struct vtx *vtx = arg;

	MAGIC_CHECK(vtx->video);

	warning("video: video-source error: %m\n", err);

	vtx->vsrc = mem_deref(vtx->vsrc);
}


static int vtx_thread(void *arg)
{
	struct vtx *vtx = arg;
	uint64_t jfs;
	uint64_t start_jfs  = tmr_jiffies_usec();
	uint64_t target_jfs = tmr_jiffies_usec();
	uint32_t bitrate;

	if (vtx->video->cfg.send_bitrate)
		bitrate = vtx->video->cfg.send_bitrate;
	else
		bitrate = vtx->video->cfg.bitrate;

	const uint64_t max_delay = PKT_SIZE * 8 * 1000000LL / bitrate + 1;
	const uint64_t max_burst =
		vtx->video->cfg.burst_bits * 1000000LL / bitrate;

	struct vidqent *qent = NULL;
	struct mbuf *mbd;
	size_t sent = 0;

	while (re_atomic_rlx(&vtx->run)) {
		mtx_lock(vtx->lock_tx);
		if (!vtx->sendq.head) {
			cnd_wait(&vtx->wait, vtx->lock_tx);
			qent = NULL;
			mtx_unlock(vtx->lock_tx);
			continue;
		}
		qent = vtx->sendq.head->data;
		mtx_unlock(vtx->lock_tx);

		jfs = tmr_jiffies_usec();

		if (jfs < target_jfs) {
			uint64_t delay = target_jfs - jfs;
			if (delay > max_delay) {
				delay	  = max_delay;
				start_jfs = jfs + delay;
				sent	  = 0;
			}
			sys_usleep((unsigned int)delay);
		}
		else {
			if (jfs - max_burst > target_jfs) {
				start_jfs = jfs - max_burst;
				sent	  = 0;
			}
		}

		sent += mbuf_get_left(qent->mb) * 8;
		target_jfs = start_jfs + sent * 1000000 / bitrate;

		mbd = mbuf_dup(qent->mb);

		stream_send(vtx->video->strm, qent->ext, qent->marker,
			    qent->pt, qent->ts, qent->mb);

		mem_deref(qent->mb);

		qent->jfs_nack = jfs + NACK_QUEUE_TIME * 1000;
		qent->seq = rtp_sess_seq(stream_rtp_sock(vtx->video->strm));
		qent->mb  = mbd;

		mtx_lock(vtx->lock_tx);
		list_move(&qent->le, &vtx->sendqnb);

		/* Delayed NACK queue cleanup */
		struct le *le = vtx->sendqnb.head;
		while (le) {
			qent = le->data;

			le = le->next;

			if (jfs > qent->jfs_nack)
				mem_deref(qent);
			else
				break; /* Assuming list is sorted by time */
		}
		mtx_unlock(vtx->lock_tx);
	}

	return 0;
}


static int vtx_alloc(struct vtx *vtx, struct video *video)
{
	int err;

	err = mutex_alloc(&vtx->lock_enc);
	if (err)
		return err;

	err = mutex_alloc(&vtx->lock_tx);
	if (err)
		return err;

	err |= cnd_init(&vtx->wait) != thrd_success;
	if (err)
		return ENOMEM;

	vtx->video = video;

	/* The initial value of the timestamp SHOULD be random */
	vtx->ts_offset = rand_u16();

	str_ncpy(vtx->device, video->cfg.src_dev, sizeof(vtx->device));

	vtx->fmt = (enum vidfmt)-1;

	return 0;
}


static int vrx_alloc(struct vrx *vrx, struct video *video)
{
	int err;

	err = mtx_init(&vrx->lock, mtx_plain) != thrd_success;
	if (err)
		return ENOMEM;

	vrx->video  = video;
	vrx->pt_rx  = -1;
	vrx->orient = VIDORIENT_PORTRAIT;

	str_ncpy(vrx->device, video->cfg.disp_dev, sizeof(vrx->device));

	vrx->fmt = (enum vidfmt)-1;

	return 0;
}


static void picup_tmr_handler(void *arg)
{
	struct vrx *vrx = arg;

	MAGIC_CHECK(vrx->video);

	request_picture_update(vrx);
}


static void send_fir(struct stream *s, bool pli)
{
	int err;

	if (pli) {
		uint32_t ssrc;

		err = stream_ssrc_rx(s, &ssrc);
		if (!err)
			err = rtcp_send_pli(stream_rtp_sock(s), ssrc);
	}
	else
		err = rtcp_send_fir(stream_rtp_sock(s),
				    rtp_sess_ssrc(stream_rtp_sock(s)));

	if (err) {
		warning("video: failed to send RTCP %s: %m\n",
			pli ? "PLI" : "FIR", err);
	}
}


static void request_picture_update(struct vrx *vrx)
{
	struct video *v = vrx->video;

	if (tmr_isrunning(&vrx->tmr_picup))
		return;

	tmr_start(&vrx->tmr_picup, PICUP_INTERVAL, picup_tmr_handler, vrx);

	/* send RTCP FIR to peer */
	send_fir(v->strm, v->nack_pli);

	++vrx->n_picup;
}


static void update_rtp_timestamp(struct timestamp_recv *tsr, uint32_t rtp_ts)
{
	int wrap;

	if (tsr->is_set) {

		wrap = timestamp_wrap(rtp_ts, tsr->last);

		switch (wrap) {

		case -1:
			info("video: rtp timestamp wraps backwards"
			     " (delta = %d) -- discard\n",
			     (int32_t)(tsr->last - rtp_ts));
			return;

		case 0:
			break;

		case 1:
			++tsr->num_wraps;
			break;

		default:
			break;
		}
	}
	else {
		timestamp_set(tsr, rtp_ts);
	}

	tsr->last = rtp_ts;
}


static void vidframe_clear(struct vidframe *frame)
{
	frame->data[0] = NULL;
}


/**
 * Decode incoming RTP packets using the Video decoder
 *
 * NOTE: mb=NULL if no packet received
 *
 * @param vrx Video receive object
 * @param hdr RTP Header
 * @param mb  Buffer with RTP payload
 *
 * @return 0 if success, otherwise errorcode
 */
static int video_stream_decode(struct vrx *vrx, const struct rtp_header *hdr,
			       struct mbuf *mb)
{
	struct video *v = vrx->video;
	struct vidframe *frame_filt = NULL;
	struct vidframe frame_store, *frame = &frame_store;
	struct viddec_packet pkt = {.mb = mb, .hdr = hdr};
	struct le *le;
	int err = 0;

	if (!hdr || !mbuf_get_left(mb))
		return 0;

	mtx_lock(&vrx->lock);

	/* No decoder set */
	if (!vrx->dec) {
		warning("video: No video decoder!\n");
		goto out;
	}

	update_rtp_timestamp(&vrx->ts_recv, hdr->ts);

	/* convert the RTP timestamp to VIDEO_TIMEBASE timestamp */
	pkt.timestamp = video_calc_timebase_timestamp(
			  timestamp_calc_extended(vrx->ts_recv.num_wraps,
						  vrx->ts_recv.last));

	vidframe_clear(frame);

	err = vrx->vc->dech(vrx->dec, frame, &pkt);
	if (err) {

		if (err != EPROTO) {
			warning("video: %s decode error"
				" (seq=%u, %u bytes): %m\n",
				vrx->vc->name, hdr->seq,
				mbuf_get_left(mb), err);
		}

		RE_TRACE_INSTANT("video", "decode_err");
		request_picture_update(vrx);

		goto out;
	}

	if (pkt.intra) {
		tmr_cancel(&vrx->tmr_picup);
		++vrx->n_intra;
	}

	/* Got a full picture-frame? */
	if (!vidframe_isvalid(frame))
		goto out;

	if (!vrx->size.w) {
		info("video: receiving with resolution %u x %u"
		     " and format '%s'\n",
		     frame->size.w, frame->size.h,
		     vidfmt_name(frame->fmt));
	}

	vrx->size = frame->size;
	vrx->fmt  = frame->fmt;

	if (!list_isempty(&vrx->filtl)) {

		err = vidframe_alloc(&frame_filt, frame->fmt, &frame->size);
		if (err)
			goto out;

		vidframe_copy(frame_filt, frame);

		frame = frame_filt;
	}

	/* Process video frame through all Video Filters */
	for (le = vrx->filtl.head; le; le = le->next) {

		struct vidfilt_dec_st *st = le->data;

		if (st->vf && st->vf->dech)
			err |= st->vf->dech(st, frame, &pkt.timestamp);
	}

	++vrx->stats.disp_frames;

	if (vrx->vd && vrx->vd->disph && vrx->vidisp)
		err = vrx->vd->disph(vrx->vidisp, v->peer, frame,
				     pkt.timestamp);

	frame_filt = mem_deref(frame_filt);
	if (err == ENODEV) {
		warning("video: video-display was closed\n");
		vrx->vidisp = mem_deref(vrx->vidisp);
		vrx->vd = NULL;

		mtx_unlock(&vrx->lock);

		if (v->errh) {
			v->errh(err, "display closed", v->arg);
		}

		return err;
	}

	++vrx->frames;

out:
	mtx_unlock(&vrx->lock);

	return err;
}


static int stream_pt_handler(uint8_t pt, struct mbuf *mb, void *arg)
{
	struct video *v = arg;
	const struct sdp_format *lc;
	(void)mb;

	if (v->vrx.pt_rx == (uint8_t)-1 || v->vrx.pt_rx == pt)
		return 0;

	if (v->vrx.pt_rx != -1)
		info("Video decoder changed payload %d -> %u\n",
		     v->vrx.pt_rx, pt);

	lc = sdp_media_lformat(stream_sdpmedia(v->strm), pt);
	if (!lc)
		return ENOENT;

	v->vrx.pt_rx = pt;
	return video_decoder_set(v, lc->data, lc->pt, lc->rparams);
}


/* Handle incoming stream data from the network */
static void stream_recv_handler(const struct rtp_header *hdr,
				struct rtpext *extv, size_t extc,
				struct mbuf *mb, unsigned lostc, bool *ignore,
				void *arg)
{
	struct video *v = arg;
	(void)extv;
	(void)extc;
	(void)ignore;

	MAGIC_CHECK(v);

	/* in case of packet loss, we need to receive a new keyframe */
	if (lostc)
		request_picture_update(&v->vrx);

	(void)video_stream_decode(&v->vrx, hdr, mb);
}


static void rtcp_nack_handler(struct vtx *vtx, struct rtcp_msg *msg)
{
	uint16_t nack_pid;
	uint16_t nack_blp;
	uint16_t pids[NACK_BLPSZ + 1];
	struct le *le;

	if (!msg || msg->hdr.count != RTCP_RTPFB_GNACK ||
	    !msg->r.fb.fci.gnackv)
		return;

	nack_pid = msg->r.fb.fci.gnackv->pid;
	nack_blp = msg->r.fb.fci.gnackv->blp;
	pids[0]	 = nack_pid;

	if (nack_blp) {
		for (int i = 1; i < NACK_BLPSZ + 1; i++) {
			if (nack_blp & (1 << (i - 1))) {
				pids[i] = nack_pid + i;
			}
		}
	}

	mtx_lock(vtx->lock_tx);
	LIST_FOREACH(&vtx->sendqnb, le) {
		struct vidqent *qent = le->data;

		if (qent->seq == nack_pid)
			break;
	}

	for (int i = 0; i < NACK_BLPSZ + 1 && le; i++) {
		struct vidqent *qent = le->data;

		le = le->next;
		if (qent->seq != pids[i])
			continue;

		debug("NACK resend rtp seq: %u\n", pids[i]);
		stream_resend(vtx->video->strm, qent->seq, qent->ext,
			      qent->marker, qent->pt, qent->ts, qent->mb);

		/* sent only once */
		mem_deref(qent);
	}

	mtx_unlock(vtx->lock_tx);
}


static void rtcp_handler(struct stream *strm, struct rtcp_msg *msg, void *arg)
{
	struct video *v = arg;
	struct vtx *vtx = &v->vtx;
	(void)strm;

	MAGIC_CHECK(v);

	switch (msg->hdr.pt) {

	case RTCP_FIR:
		mtx_lock(vtx->lock_enc);
		vtx->picup = true;
		mtx_unlock(vtx->lock_enc);
		break;

	case RTCP_PSFB:
		if (msg->hdr.count == RTCP_PSFB_PLI) {
			debug("video: recv Picture Loss Indication (PLI)\n");
			mtx_lock(vtx->lock_enc);
			vtx->picup = true;
			mtx_unlock(vtx->lock_enc);
		}
		break;

	case RTCP_RTPFB:
		rtcp_nack_handler(vtx, msg);
		break;

	default:
		break;
	}
}


static int vtx_print_pipeline(struct re_printf *pf, const struct vtx *vtx)
{
	struct le *le;
	struct vidsrc *vs;
	int err;

	if (!vtx)
		return 0;

	vs = vtx->vs;

	err = re_hprintf(pf, "video tx pipeline: %10s",
			 vs ? vs->name : "(src)");

	for (le = list_head(&vtx->filtl); le; le = le->next) {
		struct vidfilt_enc_st *st = le->data;

		if (st->vf->ench)
			err |= re_hprintf(pf, " ---> %s", st->vf->name);
	}

	err |= re_hprintf(pf, " ---> %s\n",
			  vtx->vc ? vtx->vc->name : "(encoder)");

	return err;
}


static int vrx_print_pipeline(struct re_printf *pf, const struct vrx *vrx)
{
	struct le *le;
	struct vidisp *vd;
	int err;

	if (!vrx)
		return 0;

	vd = vrx->vd;

	err = re_hprintf(pf, "video rx pipeline: %10s",
			 vd ? vd->name : "(disp)");

	for (le = list_head(&vrx->filtl); le; le = le->next) {
		struct vidfilt_dec_st *st = le->data;

		if (st->vf->dech)
			err |= re_hprintf(pf, " <--- %s", st->vf->name);
	}

	err |= re_hprintf(pf, " <--- %s\n",
			  vrx->vc ? vrx->vc->name : "(decoder)");

	return err;
}


/**
 * Allocate a video stream
 *
 * @param vp         Pointer to allocated video stream object
 * @param streaml    List of streams
 * @param stream_prm Stream parameters
 * @param cfg        Global configuration
 * @param sdp_sess   SDP Session
 * @param mnat       Media NAT (optional)
 * @param mnat_sess  Media NAT session (optional)
 * @param menc       Media Encryption (optional)
 * @param menc_sess  Media Encryption session (optional)
 * @param content    SDP content attribute
 * @param vidcodecl  List of video codecs
 * @param vidfiltl   List of video filters
 * @param offerer    True if SDP offerer, false if SDP answerer
 * @param errh       Error handler
 * @param arg        Handler argument
 *
 * @return 0 if success, otherwise errorcode
 */
int video_alloc(struct video **vp, struct list *streaml,
		const struct stream_param *stream_prm,
		const struct config *cfg,
		struct sdp_session *sdp_sess,
		const struct mnat *mnat, struct mnat_sess *mnat_sess,
		const struct menc *menc, struct menc_sess *menc_sess,
		const char *content, const struct list *vidcodecl,
		const struct list *vidfiltl,
		bool offerer,
		video_err_h *errh, void *arg)
{
	struct video *v;
	struct le *le;
	int err = 0;

	if (!vp || !cfg)
		return EINVAL;

	v = mem_zalloc(sizeof(*v), NULL);
	if (!v)
		return ENOMEM;

	MAGIC_INIT(v);

	v->cfg = cfg->video;

	err  = vtx_alloc(&v->vtx, v);
	err |= vrx_alloc(&v->vrx, v);
	if (err)
		goto out;

	mem_destructor(v, video_destructor);

	tmr_init(&v->tmr);

	err = stream_alloc(&v->strm, streaml, stream_prm,
			   &cfg->avt, sdp_sess, MEDIA_VIDEO,
			   mnat, mnat_sess, menc, menc_sess, offerer,
			   stream_recv_handler, rtcp_handler,
			   stream_pt_handler, v);
	if (err)
		goto out;

	if (vidisp_find(baresip_vidispl(), NULL) == NULL)
		stream_set_ldir(v->strm, SDP_SENDONLY);

	stream_set_srate(v->strm, VIDEO_SRATE, VIDEO_SRATE);

	if (cfg->avt.rtp_bw.max >= AUDIO_BANDWIDTH) {
		uint32_t bps = cfg->avt.rtp_bw.max - AUDIO_BANDWIDTH;

		sdp_media_set_lbandwidth(stream_sdpmedia(v->strm),
					 SDP_BANDWIDTH_AS, bps / 1000);
	}

	err |= sdp_media_set_lattr(stream_sdpmedia(v->strm), true,
				   "framerate", "%.2f", v->cfg.fps);

	/* RFC 4585 */
	err |= sdp_media_set_lattr(stream_sdpmedia(v->strm), true,
				   "rtcp-fb", "* nack");

	err |= sdp_media_set_lattr(stream_sdpmedia(v->strm), false,
				   "rtcp-fb", "* nack pli");

	/* RFC 4796 */
	if (content) {
		err |= sdp_media_set_lattr(stream_sdpmedia(v->strm), true,
					   "content", "%s", content);
	}

	if (err)
		goto out;

	v->errh = errh;
	v->arg = arg;

	/* Video codecs */
	for (le = list_head(vidcodecl); le; le = le->next) {
		struct vidcodec *vc = le->data;
		err |= sdp_format_add(NULL, stream_sdpmedia(v->strm), false,
				      vc->pt, vc->name, 90000, 1,
				      vc->fmtp_ench, vc->fmtp_cmph, vc, false,
				      "%s", vc->fmtp);
	}

	/* Video filters */
	for (le = list_head(vidfiltl); le; le = le->next) {
		struct vidfilt *vf = le->data;
		struct vidfilt_prm prmenc, prmdec;
		void *ctx = NULL;

		prmenc.width  = v->cfg.width;
		prmenc.height = v->cfg.height;
		prmenc.fmt    = v->cfg.enc_fmt;
		prmenc.fps    = get_fps(v);

		prmdec.width  = 0;
		prmdec.height = 0;
		prmdec.fmt    = -1;
		prmdec.fps    = .0;

		err |= vidfilt_enc_append(&v->vtx.filtl, &ctx, vf, &prmenc, v);
		err |= vidfilt_dec_append(&v->vrx.filtl, &ctx, vf, &prmdec, v);
		if (err) {
			warning("video: video-filter '%s' failed (%m)\n",
				vf->name, err);
			break;
		}
	}

 out:
	if (err)
		mem_deref(v);
	else
		*vp = v;

	return err;
}


static void vidisp_resize_handler(const struct vidsz *sz, void *arg)
{
	struct vrx *vrx = arg;
	(void)vrx;

	MAGIC_CHECK(vrx->video);

	info("video: display resized: %u x %u\n", sz->w, sz->h);
}


/* Set the video display - can be called multiple times */
static int set_vidisp(struct vrx *vrx)
{
	struct vidisp *vd;
	int err;

	vrx->vidisp = mem_deref(vrx->vidisp);
	vrx->vd = NULL;

	vrx->vidisp_prm.fullscreen = vrx->video->cfg.fullscreen;

	vd = (struct vidisp *)vidisp_find(baresip_vidispl(),
					  vrx->video->cfg.disp_mod);
	if (!vd)
		return ENOENT;

	err = vd->alloch(&vrx->vidisp, vd, &vrx->vidisp_prm, vrx->device,
			 vidisp_resize_handler, vrx);
	if (err)
		return err;

	vrx->vd = vd;

	return 0;
}


enum {TMR_INTERVAL = 5};
static void tmr_handler(void *arg)
{
	struct video *v = arg;

	MAGIC_CHECK(v);

	tmr_start(&v->tmr, TMR_INTERVAL * 1000, tmr_handler, v);

	/* protect vtx.frames */
	mtx_lock(v->vtx.lock_enc);

	/* Estimate framerates */
	v->vtx.efps = (double)v->vtx.frames / (double)TMR_INTERVAL;
	v->vrx.efps = (double)v->vrx.frames / (double)TMR_INTERVAL;

	v->vtx.frames = 0;
	v->vrx.frames = 0;

	mtx_unlock(v->vtx.lock_enc);
}


/**
 * Update video object and start/stop according to media direction
 *
 * @param v    Video object
 * @param peer Peer-URI as string
 *
 * @return int 0 if success, otherwise errorcode
 */
int video_update(struct video *v, const char *peer)
{
	const struct sdp_format *sc = NULL;
	enum sdp_dir dir = SDP_INACTIVE;
	struct sdp_media *m = NULL;
	int err = 0;

	if (!v)
		return EINVAL;

	m = stream_sdpmedia(v->strm);

	debug("video: update\n");

	if (!sdp_media_disabled(m)) {
		dir = sdp_media_dir(m);
		sc = sdp_media_rformat(m, NULL);
	}

	if (!sc) {
		info("video: video stream is disabled..\n");
		video_stop(v);
		return 0;
	}

	if (dir & SDP_SENDONLY)
		err = video_encoder_set(v, sc->data, sc->pt, sc->params);

	if (dir & SDP_RECVONLY)
		err |= video_decoder_set(v, sc->data, sc->pt, sc->rparams);

	/* Stop / Start source & display*/
	if (dir & SDP_SENDONLY) {
		err |= video_start_source(v);
	}
	else {
		video_stop_source(v);
	}

	if (dir == SDP_RECVONLY)
		stream_open_natpinhole(v->strm);
	else
		stream_stop_natpinhole(v->strm);

	if (dir & SDP_RECVONLY) {
		err |= video_start_display(v, peer);
		stream_enable_rx(v->strm, true);
	}
	else {
		stream_enable_rx(v->strm, false);
		video_stop_display(v);
	}

	if (err)
		warning("video: video stream error: %m\n", err);

	return err;
}


/**
 * Start the video source
 *
 * @param v   Video object
 *
 * @return 0 if success, otherwise errorcode
 */
int video_start_source(struct video *v)
{
	struct vidsz size;
	int err;

	if (!v)
		return EINVAL;

	if (v->vtx.vsrc)
		return 0;

	struct vtx *vtx = &v->vtx;

	debug("video: start source\n");

	if (vidsrc_find(baresip_vidsrcl(), NULL)) {
		struct vidsrc *vs;

		vs = (struct vidsrc *)vidsrc_find(baresip_vidsrcl(),
						  v->cfg.src_mod);
		if (!vs) {
			warning("video: source not found: %s\n",
				v->cfg.src_mod);
			return ENOENT;
		}

		size.w = v->cfg.width;
		size.h = v->cfg.height;

		vtx->vsrc_size       = size;
		vtx->vsrc_prm.fps    = get_fps(v);
		vtx->vsrc_prm.fmt    = v->cfg.enc_fmt;

		vtx->vsrc = mem_deref(vtx->vsrc);

		err = vs->alloch(&vtx->vsrc, vs, &vtx->vsrc_prm,
				 &vtx->vsrc_size, NULL, v->vtx.device,
				 vidsrc_frame_handler, vidsrc_packet_handler,
				 vidsrc_error_handler, vtx);
		if (err) {
			warning("video: could not set source to"
				" [%u x %u] %m\n",
				size.w, size.h, err);
		}

		vtx->vs = vs;
		if (v->vtx.vc)
			info("%H", vtx_print_pipeline, &v->vtx);
	}
	else {
		info("video: no video source\n");
	}

	if (!re_atomic_rlx(&vtx->run)) {
		re_atomic_rlx_set(&vtx->run, true);
		thread_create_name(&vtx->thrd, "Video TX", vtx_thread, vtx);
	}
	else {
		warning("video_start_source: Video TX already started\n");
	}

	stream_enable_tx(v->strm, true);
	tmr_start(&v->tmr, TMR_INTERVAL * 1000, tmr_handler, v);

	return 0;
}


/**
 * Start the video display
 *
 * @param v    Video object
 * @param peer Peer name
 *
 * @return 0 if success, otherwise errorcode
 */
int video_start_display(struct video *v, const char *peer)
{
	int err;

	if (!v)
		return EINVAL;

	if (v->vrx.vidisp)
		return 0;

	debug("video: start display\n");

	if (peer) {
		v->peer = mem_deref(v->peer);
		err = str_dup(&v->peer, peer);
		if (err)
			return err;
	}

	if (vidisp_find(baresip_vidispl(), NULL)) {
		err = set_vidisp(&v->vrx);
		if (err) {
			warning("video: could not set vidisp '%s': %m\n",
				v->vrx.device, err);
			return err;
		}

		if (v->vrx.vc)
			info("%H", vrx_print_pipeline, &v->vrx);
	}
	else {
		info("video: no video display\n");
	}

	return 0;
}


/**
 * Stop the video source
 *
 * @param v   Video object
 */
static void video_stop_source(struct video *v)
{
	if (!v)
		return;

	debug("video: stopping video source ..\n");

	stream_enable_tx(v->strm, false);
	v->vtx.vsrc = mem_deref(v->vtx.vsrc);

	if (re_atomic_rlx(&v->vtx.run)) {
		re_atomic_rlx_set(&v->vtx.run, false);
		cnd_signal(&v->vtx.wait);
		thrd_join(v->vtx.thrd, NULL);
	}

	mtx_lock(v->vtx.lock_tx);
	list_flush(&v->vtx.sendq);
	list_flush(&v->vtx.sendqnb);
	mtx_unlock(v->vtx.lock_tx);
}


/**
 * Stop the video display
 *
 * @param v   Video object
 */
void video_stop_display(struct video *v)
{
	if (!v)
		return;

	debug("video: stopping video display ..\n");

	v->vrx.vidisp = mem_deref(v->vrx.vidisp);
}


/**
 * Stop video sourc & display
 *
 * @param v   Video object
 */
void video_stop(struct video *v)
{
	video_stop_source(v);
	video_stop_display(v);
}


static int vidisp_update(struct vrx *vrx)
{
	struct vidisp *vd = vrx->vd;
	int err = 0;

	if (vd->updateh) {
		err = vd->updateh(vrx->vidisp, vrx->vidisp_prm.fullscreen,
				  vrx->orient, NULL);
	}

	return err;
}


/**
 * Enable video display fullscreen
 *
 * @param v  Video stream
 * @param fs True for fullscreen, otherwise false
 *
 * @return 0 if success, otherwise errorcode
 */
int video_set_fullscreen(struct video *v, bool fs)
{
	if (!v)
		return EINVAL;

	v->vrx.vidisp_prm.fullscreen = fs;

	return vidisp_update(&v->vrx);
}


static void vidsrc_update(struct vtx *vtx, const char *dev)
{
	struct vidsrc *vs = vtx->vs;

	if (vs && vs->updateh)
		vs->updateh(vtx->vsrc, &vtx->vsrc_prm, dev);
}


/**
 * Set the video encoder used
 *
 * @param v      Video object
 * @param vc     Video codec to use
 * @param pt_tx  Payload type for sending
 * @param params Optional encoder parameters
 *
 * @return 0 if success, otherwise errorcode
 */
int video_encoder_set(struct video *v, struct vidcodec *vc,
		      int pt_tx, const char *params)
{
	struct vtx *vtx;
	int err = 0;

	if (!v)
		return EINVAL;

	vtx = &v->vtx;

	if (!vc->encupdh) {
		info("video: vidcodec '%s' has no encoder\n", vc->name);
		return ENOENT;
	}

	mtx_lock(vtx->lock_enc);

	if (vc != vtx->vc) {

		struct videnc_param prm;

		prm.bitrate = v->cfg.bitrate;
		prm.pktsize = PKT_SIZE;
		prm.fps     = get_fps(v);
		prm.max_fs  = -1;

		info("Set video encoder: %s %s (%u bit/s, %.2f fps)\n",
		     vc->name, vc->variant, prm.bitrate, prm.fps);

		vtx->enc = mem_deref(vtx->enc);
		err = vc->encupdh(&vtx->enc, vc, &prm, params,
				  packet_handler, v);
		if (err) {
			warning("video: encoder alloc: %m\n", err);
			goto out;
		}

		vtx->vc = vc;
	}

	stream_update_encoder(v->strm, pt_tx);

 out:
	mtx_unlock(vtx->lock_enc);

	return err;
}


int video_decoder_set(struct video *v, struct vidcodec *vc, int pt_rx,
		      const char *fmtp)
{
	struct vrx *vrx;
	int err = 0;

	if (!v || !vc)
		return EINVAL;

	/* handle vidcodecs without a decoder */
	if (!vc->decupdh) {
		struct list *vidcodecl = vc->le.list;
		struct vidcodec *vcd;

		info("video: vidcodec '%s' has no decoder\n", vc->name);

		vcd = (struct vidcodec *)vidcodec_find_decoder(vidcodecl,
							       vc->name);
		if (!vcd) {
			warning("video: could not find decoder (%s)\n",
				vc->name);
			return ENOENT;
		}

		vc = vcd;
	}

	vrx = &v->vrx;

	vrx->pt_rx = pt_rx;

	if (vc != vrx->vc) {

		info("Set video decoder: %s %s\n", vc->name, vc->variant);

		vrx->dec = mem_deref(vrx->dec);

		err = vc->decupdh(&vrx->dec, vc, fmtp, v);
		if (err) {
			warning("video: decoder alloc: %m\n", err);
			return err;
		}

		vrx->vc = vc;
	}

	return err;
}


/**
 * Get the RTP Stream object from a Video object
 *
 * @param v  Video object
 *
 * @return RTP Stream object
 */
struct stream *video_strm(const struct video *v)
{
	return v ? v->strm : NULL;
}


/**
 * Set the current Video Source device name
 *
 * @param v   Video stream
 * @param dev Device name
 */
void video_vidsrc_set_device(struct video *v, const char *dev)
{
	if (!v)
		return;

	vidsrc_update(&v->vtx, dev);
}


static bool nack_handler(const char *name, const char *value, void *arg)
{
	(void)name;
	(void)arg;

	return 0 == re_regex(value, str_len(value), "nack");
}


void video_sdp_attr_decode(struct video *v)
{
	if (!v)
		return;

	/* RFC 4585 */
	if (sdp_media_rattr_apply(stream_sdpmedia(v->strm), "rtcp-fb",
				  nack_handler, 0))
		v->nack_pli = true;
}


static int vtx_debug(struct re_printf *pf, const struct vtx *vtx)
{
	int err = 0;

	err |= re_hprintf(pf, " tx: encode: %s %s\n",
			  vtx->vc ? vtx->vc->name : "none",
			  vidfmt_name(vtx->fmt));

	mtx_lock(vtx->lock_enc);
	err |= re_hprintf(pf, "     source: %s %u x %u, fps=%.2f"
			  " frames=%llu\n",
			  vtx->vs ? vtx->vs->name : "none",
			  vtx->vsrc_size.w,
			  vtx->vsrc_size.h, vtx->vsrc_prm.fps,
			  vtx->stats.src_frames);
	mtx_unlock(vtx->lock_enc);

	mtx_lock(vtx->lock_tx);
	err |= re_hprintf(pf, "     skipc=%u sendq=%u\n",
			  vtx->skipc, list_count(&vtx->sendq));

	if (vtx->ts_base) {
		err |= re_hprintf(pf, "     time = %.3f sec\n",
			  video_calc_seconds(vtx->ts_last - vtx->ts_base));
	}
	else {
		err |= re_hprintf(pf, "     time = (not started)\n");
	}
	mtx_unlock(vtx->lock_tx);

	return err;
}


static int vrx_debug(struct re_printf *pf, const struct vrx *vrx)
{
	int err = 0;

	err |= re_hprintf(pf, " rx: decode: %s %s\n",
			  vrx->vc ? vrx->vc->name : "none",
			  vidfmt_name(vrx->fmt));
	err |= re_hprintf(pf, "     vidisp: %s %u x %u frames=%llu\n",
			  vrx->vd ? vrx->vd->name : "none",
			  vrx->size.w, vrx->size.h,
			  vrx->stats.disp_frames);
	err |= re_hprintf(pf, "     n_keyframes=%u, n_picup=%u\n",
			  vrx->n_intra, vrx->n_picup);

	if (vrx->ts_recv.is_set) {
		err |= re_hprintf(pf, "     time = %.3f sec\n",
		  video_calc_seconds(timestamp_duration(&vrx->ts_recv)));
	}
	else {
		err |= re_hprintf(pf, "     time = (not started)\n");
	}

	return err;
}


/**
 * Print the video debug information
 *
 * @param pf   Print function
 * @param v    Video object
 *
 * @return 0 if success, otherwise errorcode
 */
int video_debug(struct re_printf *pf, const struct video *v)
{
	const struct vtx *vtx;
	const struct vrx *vrx;
	int err;

	if (!v)
		return 0;

	vtx = &v->vtx;
	vrx = &v->vrx;

	err = re_hprintf(pf, "\n--- Video stream ---\n");
	err |= re_hprintf(pf, " source started: %s\n",
		v->vtx.vsrc ? "yes" : "no");
	err |= re_hprintf(pf, " display started: %s\n",
		v->vrx.vidisp ? "yes" : "no");

	err |= vtx_debug(pf, vtx);
	err |= vrx_debug(pf, vrx);
	if (err)
		return err;

	if (!list_isempty(&vtx->filtl))
		err |= vtx_print_pipeline(pf, vtx);
	if (!list_isempty(&vrx->filtl))
		err |= vrx_print_pipeline(pf, vrx);

	err |= stream_debug(pf, v->strm);

	return err;
}


int video_print(struct re_printf *pf, const struct video *v)
{
	if (!v)
		return 0;

	return re_hprintf(pf, " efps=%.1f/%.1f", v->vtx.efps, v->vrx.efps);
}


/**
 * Set the active video source
 *
 * @param v    Video object
 * @param name Video source name
 * @param dev  Video source device
 *
 * @return 0 if success, otherwise errorcode
 */
int video_set_source(struct video *v, const char *name, const char *dev)
{
	struct vidsrc *vs = (struct vidsrc *)vidsrc_find(baresip_vidsrcl(),
							 name);
	struct vtx *vtx;
	int err;

	if (!v)
		return EINVAL;

	if (!vs)
		return ENOENT;

	vtx = &v->vtx;

	vtx->vsrc = mem_deref(vtx->vsrc);

	err = vs->alloch(&vtx->vsrc, vs, &vtx->vsrc_prm,
			 &vtx->vsrc_size, NULL, dev,
			 vidsrc_frame_handler, vidsrc_packet_handler,
			 vidsrc_error_handler, vtx);
	if (err)
		return err;

	vtx->vs = vs;

	return 0;
}


/**
 * Set the device name of video source and display
 *
 * @param v    Video object
 * @param src  Video source device
 * @param disp Video display device
 */
void video_set_devicename(struct video *v, const char *src, const char *disp)
{
	if (!v)
		return;

	str_ncpy(v->vtx.device, src, sizeof(v->vtx.device));
	str_ncpy(v->vrx.device, disp, sizeof(v->vrx.device));
}


/**
 * Get the device name of video source
 *
 * @param v    Video object
 *
 * @return Video source device name, otherwise NULL
 */
const char *video_get_src_dev(const struct video *v)
{
	if (!v)
		return NULL;

	return v->vtx.device;
}


/**
 * Get the device name of video display
 *
 * @param v    Video object
 *
 * @return Video display device name, otherwise NULL
 */
const char *video_get_disp_dev(const struct video *v)
{
	if (!v)
		return NULL;

	return v->vrx.device;
}


/**
 * Get video codec of video stream
 *
 * @param vid Video object
 * @param tx  True to get transmit codec, false to get receive codec
 *
 * @return Video codec if success, otherwise NULL
 */
const struct vidcodec *video_codec(const struct video *vid, bool tx)
{
	if (!vid)
		return NULL;

	return tx ? vid->vtx.vc : vid->vrx.vc;
}


/**
 * Request new keyframe from encoder (vtx)
 *
 * @param vid Video object
 */
void video_req_keyframe(struct video *vid)
{
	if (!vid)
		return;

	mtx_lock(vid->vtx.lock_enc);
	vid->vtx.picup = true;
	mtx_unlock(vid->vtx.lock_enc);
}
