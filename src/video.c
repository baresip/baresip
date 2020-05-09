/**
 * @file src/video.c  Video stream
 *
 * Copyright (C) 2010 Creytiv.com
 *
 * \ref GenericVideoStream
 */
#include <string.h>
#include <stdlib.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "core.h"


/** Magic number */
#define MAGIC 0x00070d10
#include "magic.h"


/** Video transmit parameters */
enum {
	MEDIA_POLL_RATE = 250,                 /**< in [Hz]             */
	BURST_MAX       = 8192,                /**< in bytes            */
	RTP_PRESZ       = 4 + RTP_HEADER_SIZE, /**< TURN and RTP header */
	RTP_TRAILSZ     = 12 + 4,              /**< SRTP/SRTCP trailer  */
	PICUP_INTERVAL  = 500,
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
	struct vidsrc_st *vsrc;            /**< Video source              */
	struct lock *lock_enc;             /**< Lock for encoder          */
	struct vidframe *frame;            /**< Source frame              */
	struct lock *lock_tx;              /**< Protect the sendq         */
	struct list sendq;                 /**< Tx-Queue (struct vidqent) */
	struct tmr tmr_rtp;                /**< Timer for sending RTP     */
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
	struct vidisp_st *vidisp;          /**< Video display             */
	struct lock *lock;                 /**< Lock for decoder          */
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
	bool started;           /**< True if video is started             */
	char *peer;             /**< Peer URI                             */
	bool nack_pli;          /**< Send NACK/PLI to peer                */
	video_err_h *errh;      /**< Error handler                        */
	void *arg;              /**< Error handler argument               */
};


struct vidqent {
	struct le le;
	struct sa dst;
	bool marker;
	uint8_t pt;
	uint32_t ts;
	struct mbuf *mb;
};


static void request_picture_update(struct vrx *vrx);


static void vidqent_destructor(void *arg)
{
	struct vidqent *qent = arg;

	list_unlink(&qent->le);
	mem_deref(qent->mb);
}


static int vidqent_alloc(struct vidqent **qentp,
			 bool marker, uint8_t pt, uint32_t ts,
			 const uint8_t *hdr, size_t hdr_len,
			 const uint8_t *pld, size_t pld_len)
{
	struct vidqent *qent;
	int err = 0;

	if (!qentp || !pld)
		return EINVAL;

	qent = mem_zalloc(sizeof(*qent), vidqent_destructor);
	if (!qent)
		return ENOMEM;

	qent->marker = marker;
	qent->pt     = pt;
	qent->ts     = ts;

	qent->mb = mbuf_alloc(RTP_PRESZ + hdr_len + pld_len + RTP_TRAILSZ);
	if (!qent->mb) {
		err = ENOMEM;
		goto out;
	}

	qent->mb->pos = qent->mb->end = RTP_PRESZ;

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


static void vidqueue_poll(struct vtx *vtx, uint64_t jfs, uint64_t prev_jfs)
{
	size_t burst, sent;
	uint64_t bandwidth_kbps;
	struct le *le;

	if (!vtx)
		return;

	lock_write_get(vtx->lock_tx);

	le = vtx->sendq.head;
	if (!le)
		goto out;

	/*
	 * time [ms] * bitrate [kbps] / 8 = bytes
	 */
	bandwidth_kbps = vtx->video->cfg.bitrate / 1000;
	burst = (size_t)((1 + jfs - prev_jfs) * bandwidth_kbps / 4);

	burst = min(burst, BURST_MAX);
	sent  = 0;

	while (le) {

		struct vidqent *qent = le->data;

		sent += mbuf_get_left(qent->mb);

		stream_send(vtx->video->strm, false, qent->marker, qent->pt,
			    qent->ts, qent->mb);

		le = le->next;
		mem_deref(qent);

		if (sent > burst) {
			break;
		}
	}

 out:
	lock_rel(vtx->lock_tx);
}


static void rtp_tmr_handler(void *arg)
{
	struct vtx *vtx = arg;
	uint64_t pjfs;

	pjfs = vtx->tmr_rtp.jfs;

	tmr_start(&vtx->tmr_rtp, 1000/MEDIA_POLL_RATE, rtp_tmr_handler, vtx);

	vidqueue_poll(vtx, vtx->tmr_rtp.jfs, pjfs);
}


static void video_destructor(void *arg)
{
	struct video *v = arg;
	struct vtx *vtx = &v->vtx;
	struct vrx *vrx = &v->vrx;

	/* transmit */
	lock_write_get(vtx->lock_tx);
	list_flush(&vtx->sendq);
	lock_rel(vtx->lock_tx);
	mem_deref(vtx->lock_tx);

	tmr_cancel(&vtx->tmr_rtp);
	mem_deref(vtx->vsrc);
	lock_write_get(vtx->lock_enc);
	mem_deref(vtx->frame);
	mem_deref(vtx->enc);
	list_flush(&vtx->filtl);
	lock_rel(vtx->lock_enc);
	mem_deref(vtx->lock_enc);

	/* receive */
	tmr_cancel(&vrx->tmr_picup);
	lock_write_get(vrx->lock);
	mem_deref(vrx->dec);
	mem_deref(vrx->vidisp);
	list_flush(&vrx->filtl);
	lock_rel(vrx->lock);
	mem_deref(vrx->lock);

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
			  void *arg)
{
	struct vtx *vtx = arg;
	struct stream *strm = vtx->video->strm;
	struct vidqent *qent;
	uint32_t rtp_ts;
	int err;

	MAGIC_CHECK(vtx->video);

	if (!vtx->ts_base)
		vtx->ts_base = ts;
	vtx->ts_last = ts;

	/* add random timestamp offset */
	rtp_ts = vtx->ts_offset + (ts & 0xffffffff);

	err = vidqent_alloc(&qent, marker, strm->pt_enc, rtp_ts,
			    hdr, hdr_len, pld, pld_len);
	if (err)
		return err;

	lock_write_get(vtx->lock_tx);
	qent->dst = *sdp_media_raddr(strm->sdp);
	list_append(&vtx->sendq, &qent->le, qent);
	lock_rel(vtx->lock_tx);

	return err;
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
			    uint64_t timestamp)
{
	struct le *le;
	int err = 0;
	bool sendq_empty;

	if (!vtx->enc)
		return;

	lock_write_get(vtx->lock_tx);
	sendq_empty = (vtx->sendq.head == NULL);
	lock_rel(vtx->lock_tx);

	if (!sendq_empty) {
		++vtx->skipc;
		return;
	}

	lock_write_get(vtx->lock_enc);

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
	lock_rel(vtx->lock_enc);
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

	lock_write_get(vtx->lock_enc);
	++vtx->frames;
	lock_rel(vtx->lock_enc);

	++vtx->stats.src_frames;

	/* Encode and send */
	encode_rtp_send(vtx, frame, timestamp);
}


static void vidsrc_error_handler(int err, void *arg)
{
	struct vtx *vtx = arg;

	MAGIC_CHECK(vtx->video);

	warning("video: video-source error: %m\n", err);

	vtx->vsrc = mem_deref(vtx->vsrc);
}


static int vtx_alloc(struct vtx *vtx, struct video *video)
{
	int err;

	err  = lock_alloc(&vtx->lock_enc);
	err |= lock_alloc(&vtx->lock_tx);
	if (err)
		return err;

	tmr_init(&vtx->tmr_rtp);

	vtx->video = video;

	/* The initial value of the timestamp SHOULD be random */
	vtx->ts_offset = rand_u16();

	str_ncpy(vtx->device, video->cfg.src_dev, sizeof(vtx->device));

	tmr_start(&vtx->tmr_rtp, 1, rtp_tmr_handler, vtx);

	vtx->fmt = (enum vidfmt)-1;

	return err;
}


static int vrx_alloc(struct vrx *vrx, struct video *video)
{
	int err;

	err = lock_alloc(&vrx->lock);
	if (err)
		return err;

	vrx->video  = video;
	vrx->pt_rx  = -1;
	vrx->orient = VIDORIENT_PORTRAIT;

	str_ncpy(vrx->device, video->cfg.disp_dev, sizeof(vrx->device));

	vrx->fmt = (enum vidfmt)-1;

	return err;
}


static void picup_tmr_handler(void *arg)
{
	struct vrx *vrx = arg;

	MAGIC_CHECK(vrx->video);

	request_picture_update(vrx);
}


static void request_picture_update(struct vrx *vrx)
{
	struct video *v = vrx->video;

	if (tmr_isrunning(&vrx->tmr_picup))
		return;

	tmr_start(&vrx->tmr_picup, PICUP_INTERVAL, picup_tmr_handler, vrx);

	/* send RTCP FIR to peer */
	stream_send_fir(v->strm, v->nack_pli);

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
	struct le *le;
	uint64_t timestamp;
	bool intra;
	int err = 0;

	if (!hdr || !mbuf_get_left(mb))
		return 0;

	lock_write_get(vrx->lock);

	/* No decoder set */
	if (!vrx->dec) {
		warning("video: No video decoder!\n");
		goto out;
	}

	update_rtp_timestamp(&vrx->ts_recv, hdr->ts);

	/* convert the RTP timestamp to VIDEO_TIMEBASE timestamp */
	timestamp = video_calc_timebase_timestamp(
			  timestamp_calc_extended(vrx->ts_recv.num_wraps,
						  vrx->ts_recv.last));

	frame->data[0] = NULL;
	err = vrx->vc->dech(vrx->dec, frame, &intra, hdr->m, hdr->seq, mb);
	if (err) {

		if (err != EPROTO) {
			warning("video: %s decode error"
				" (seq=%u, %u bytes): %m\n",
				vrx->vc->name, hdr->seq,
				mbuf_get_left(mb), err);
		}

		request_picture_update(vrx);

		goto out;
	}

	if (intra) {
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
			err |= st->vf->dech(st, frame, &timestamp);
	}

	++vrx->stats.disp_frames;

	err = vidisp_display(vrx->vidisp, v->peer, frame, timestamp);
	frame_filt = mem_deref(frame_filt);
	if (err == ENODEV) {
		warning("video: video-display was closed\n");
		vrx->vidisp = mem_deref(vrx->vidisp);

		lock_rel(vrx->lock);

		if (v->errh) {
			v->errh(err, "display closed", v->arg);
		}

		return err;
	}

	++vrx->frames;

out:
	lock_rel(vrx->lock);

	return err;
}


static int update_payload_type(struct video *v, uint8_t pt_old, uint8_t pt_new)
{
	const struct sdp_format *lc;

	lc = sdp_media_lformat(stream_sdpmedia(v->strm), pt_new);
	if (!lc)
		return ENOENT;

	if (pt_old != (uint8_t)-1) {
		info("Video decoder changed payload %u -> %u\n",
		     pt_old, pt_new);
	}

	v->vrx.pt_rx = pt_new;

	return video_decoder_set(v, lc->data, lc->pt, lc->rparams);
}


/* Handle incoming stream data from the network */
static void stream_recv_handler(const struct rtp_header *hdr,
				struct rtpext *extv, size_t extc,
				struct mbuf *mb, unsigned lostc, void *arg)
{
	struct video *v = arg;
	int err;
	(void)extv;
	(void)extc;

	MAGIC_CHECK(v);

	/* in case of packet loss, we need to receive a new keyframe */
	if (lostc)
		request_picture_update(&v->vrx);

	if (!mb)
		goto out;

	/* Video payload-type changed? */
	if (hdr->pt == v->vrx.pt_rx)
		goto out;

	err = update_payload_type(v, v->vrx.pt_rx, hdr->pt);
	if (err)
		return;

 out:
	(void)video_stream_decode(&v->vrx, hdr, mb);
}


static void rtcp_handler(struct stream *strm, struct rtcp_msg *msg, void *arg)
{
	struct video *v = arg;
	(void)strm;

	MAGIC_CHECK(v);

	switch (msg->hdr.pt) {

	case RTCP_FIR:
		v->vtx.picup = true;
		break;

	case RTCP_PSFB:
		if (msg->hdr.count == RTCP_PSFB_PLI)
			v->vtx.picup = true;
		break;

	case RTCP_RTPFB:
		if (msg->hdr.count == RTCP_RTPFB_GNACK)
			v->vtx.picup = true;
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

	vs = vidsrc_get(vtx->vsrc);

	err = re_hprintf(pf, "video tx pipeline: %10s",
			 vs ? vs->name : "src");

	for (le = list_head(&vtx->filtl); le; le = le->next) {
		struct vidfilt_enc_st *st = le->data;

		if (st->vf->ench)
			err |= re_hprintf(pf, " ---> %s", st->vf->name);
	}

	err |= re_hprintf(pf, " ---> %s\n",
			  vtx->vc ? vtx->vc->name : "encoder");

	return err;
}


static int vrx_print_pipeline(struct re_printf *pf, const struct vrx *vrx)
{
	struct le *le;
	struct vidisp *vd;
	int err;

	if (!vrx)
		return 0;

	vd = vidisp_get(vrx->vidisp);

	err = re_hprintf(pf, "video rx pipeline: %10s",
			 vd ? vd->name : "disp");

	for (le = list_head(&vrx->filtl); le; le = le->next) {
		struct vidfilt_dec_st *st = le->data;

		if (st->vf->dech)
			err |= re_hprintf(pf, " <--- %s", st->vf->name);
	}

	err |= re_hprintf(pf, " <--- %s\n",
			  vrx->vc ? vrx->vc->name : "decoder");

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
 * @param label      SDP label
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
		struct sdp_session *sdp_sess, int label,
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

	v = mem_zalloc(sizeof(*v), video_destructor);
	if (!v)
		return ENOMEM;

	MAGIC_INIT(v);

	v->cfg = cfg->video;
	tmr_init(&v->tmr);

	err = stream_alloc(&v->strm, streaml, stream_prm,
			   &cfg->avt, sdp_sess, MEDIA_VIDEO, label,
			   mnat, mnat_sess, menc, menc_sess, offerer,
			   stream_recv_handler, rtcp_handler, v);
	if (err)
		goto out;

	if (vidisp_find(baresip_vidispl(), NULL) == NULL)
		sdp_media_set_ldir(v->strm->sdp, SDP_SENDONLY);

	stream_set_srate(v->strm, VIDEO_SRATE, VIDEO_SRATE);

	if (cfg->avt.rtp_bw.max >= AUDIO_BANDWIDTH) {
		stream_set_bw(v->strm, cfg->avt.rtp_bw.max - AUDIO_BANDWIDTH);
	}

	err |= sdp_media_set_lattr(stream_sdpmedia(v->strm), true,
				   "framerate", "%.2f", v->cfg.fps);

	/* RFC 4585 */
	err |= sdp_media_set_lattr(stream_sdpmedia(v->strm), true,
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

	err  = vtx_alloc(&v->vtx, v);
	err |= vrx_alloc(&v->vrx, v);
	if (err)
		goto out;

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
		struct vidfilt_prm prm;
		void *ctx = NULL;

		prm.width  = v->cfg.width;
		prm.height = v->cfg.height;
		prm.fmt    = v->cfg.enc_fmt;
		prm.fps    = get_fps(v);

		err |= vidfilt_enc_append(&v->vtx.filtl, &ctx, vf, &prm, v);
		err |= vidfilt_dec_append(&v->vrx.filtl, &ctx, vf, &prm, v);
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

	vrx->vidisp = mem_deref(vrx->vidisp);

	vrx->vidisp_prm.fullscreen = vrx->video->cfg.fullscreen;

	vd = (struct vidisp *)vidisp_find(baresip_vidispl(),
					  vrx->video->cfg.disp_mod);
	if (!vd)
		return ENOENT;

	return vd->alloch(&vrx->vidisp, vd, &vrx->vidisp_prm, vrx->device,
			  vidisp_resize_handler, vrx);
}


enum {TMR_INTERVAL = 5};
static void tmr_handler(void *arg)
{
	struct video *v = arg;

	MAGIC_CHECK(v);

	tmr_start(&v->tmr, TMR_INTERVAL * 1000, tmr_handler, v);

	/* protect vtx.frames */
	lock_write_get(v->vtx.lock_enc);

	/* Estimate framerates */
	v->vtx.efps = (double)v->vtx.frames / (double)TMR_INTERVAL;
	v->vrx.efps = (double)v->vrx.frames / (double)TMR_INTERVAL;

	v->vtx.frames = 0;
	v->vrx.frames = 0;

	lock_rel(v->vtx.lock_enc);
}


/**
 * Start the video source
 *
 * @param v   Video object
 * @param ctx Media context
 *
 * @return 0 if success, otherwise errorcode
 */
int video_start_source(struct video *v, struct media_ctx **ctx)
{
	struct vtx *vtx = &v->vtx;
	struct vidsz size;
	int err;

	if (!v)
		return EINVAL;

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

		err = vs->alloch(&vtx->vsrc, vs, ctx, &vtx->vsrc_prm,
				 &vtx->vsrc_size, NULL, v->vtx.device,
				 vidsrc_frame_handler,
				 vidsrc_error_handler, vtx);
		if (err) {
			warning("video: could not set source to"
				" [%u x %u] %m\n",
				size.w, size.h, err);
		}
	}
	else {
		info("video: no video source\n");
	}

	tmr_start(&v->tmr, TMR_INTERVAL * 1000, tmr_handler, v);

	if (v->vtx.vc && v->vrx.vc) {
		info("%H%H",
		     vtx_print_pipeline, &v->vtx,
		     vrx_print_pipeline, &v->vrx);
	}

	v->started = true;

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
void video_stop(struct video *v)
{
	if (!v)
		return;

	debug("video: stopping video source ..\n");

	v->started = false;
	v->vtx.vsrc = mem_deref(v->vtx.vsrc);
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


bool video_is_started(const struct video *v)
{
	return v ? v->started : false;
}


static int vidisp_update(struct vrx *vrx)
{
	struct vidisp *vd = vidisp_get(vrx->vidisp);
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
	struct vidsrc *vs = vidsrc_get(vtx->vsrc);

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

	lock_write_get(vtx->lock_enc);

	if (vc != vtx->vc) {

		struct videnc_param prm;

		prm.bitrate = v->cfg.bitrate;
		prm.pktsize = 1024;
		prm.fps     = get_fps(v);
		prm.max_fs  = -1;

		info("Set video encoder: %s %s (%u bit/s, %.2f fps)\n",
		     vc->name, vc->variant, prm.bitrate, prm.fps);

		vtx->enc = mem_deref(vtx->enc);
		err = vc->encupdh(&vtx->enc, vc, &prm, params,
				  packet_handler, vtx);
		if (err) {
			warning("video: encoder alloc: %m\n", err);
			goto out;
		}

		vtx->vc = vc;
	}

	stream_update_encoder(v->strm, pt_tx);

 out:
	lock_rel(vtx->lock_enc);

	return err;
}


int video_decoder_set(struct video *v, struct vidcodec *vc, int pt_rx,
		      const char *fmtp)
{
	struct vrx *vrx;
	int err = 0;

	if (!v)
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

		err = vc->decupdh(&vrx->dec, vc, fmtp);
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


void video_update_picture(struct video *v)
{
	if (!v)
		return;
	v->vtx.picup = true;
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
	if (sdp_media_rattr_apply(v->strm->sdp, "rtcp-fb", nack_handler, 0))
		v->nack_pli = true;
}


static int vtx_debug(struct re_printf *pf, const struct vtx *vtx)
{
	int err = 0;

	err |= re_hprintf(pf, " tx: encode: %s %s\n",
			  vtx->vc ? vtx->vc->name : "none",
			  vidfmt_name(vtx->fmt));
	err |= re_hprintf(pf, "     source: %s %u x %u, fps=%.2f"
			  " frames=%llu\n",
			  vtx->vsrc ? vidsrc_get(vtx->vsrc)->name : "none",
			  vtx->vsrc_size.w,
			  vtx->vsrc_size.h, vtx->vsrc_prm.fps,
			  vtx->stats.src_frames);
	err |= re_hprintf(pf, "     skipc=%u sendq=%u\n",
			  vtx->skipc, list_count(&vtx->sendq));

	if (vtx->ts_base) {
		err |= re_hprintf(pf, "     time = %.3f sec\n",
			  video_calc_seconds(vtx->ts_last - vtx->ts_base));
	}
	else {
		err |= re_hprintf(pf, "     time = (not started)\n");
	}

	return err;
}


static int vrx_debug(struct re_printf *pf, const struct vrx *vrx)
{
	int err = 0;

	err |= re_hprintf(pf, " rx: decode: %s %s\n",
			  vrx->vc ? vrx->vc->name : "none",
			  vidfmt_name(vrx->fmt));
	err |= re_hprintf(pf, "     vidisp: %s %u x %u frames=%llu\n",
			  vrx->vidisp ? vidisp_get(vrx->vidisp)->name : "none",
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
	err |= re_hprintf(pf, " started: %s\n", v->started ? "yes" : "no");

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

	if (!v)
		return EINVAL;

	if (!vs)
		return ENOENT;

	vtx = &v->vtx;

	vtx->vsrc = mem_deref(vtx->vsrc);

	return vs->alloch(&vtx->vsrc, vs, NULL, &vtx->vsrc_prm,
			  &vtx->vsrc_size, NULL, dev,
			  vidsrc_frame_handler, vidsrc_error_handler, vtx);
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
