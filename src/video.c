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


/** Internal video-encoder format */
#ifndef VIDENC_INTERNAL_FMT
#define VIDENC_INTERNAL_FMT (VID_FMT_YUV420P)
#endif


enum {
	SRATE = 90000,
	MAX_MUTED_FRAMES = 3,
};

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
	struct lock *lock;                 /**< Lock for encoder          */
	struct vidframe *frame;            /**< Source frame              */
	struct vidframe *mute_frame;       /**< Frame with muted video    */
	struct lock *lock_tx;              /**< Protect the sendq */
	struct list sendq;                 /**< Tx-Queue (struct vidqent) */
	struct tmr tmr_rtp;                /**< Timer for sending RTP     */
	unsigned skipc;                    /**< Number of frames skipped */
	struct list filtl;                 /**< Filters in encoding order */
	char device[64];
	int muted_frames;                  /**< # of muted frames sent    */
	uint32_t ts_tx;                    /**< Outgoing RTP timestamp    */
	bool picup;                        /**< Send picture update       */
	bool muted;                        /**< Muted flag                */
	int frames;                        /**< Number of frames sent     */
	int efps;                          /**< Estimated frame-rate      */
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
	enum vidorient orient;             /**< Display orientation       */
	char device[64];
	bool fullscreen;                   /**< Fullscreen flag           */
	int pt_rx;                         /**< Incoming RTP payload type */
	int frames;                        /**< Number of frames received */
	int efps;                          /**< Estimated frame-rate      */
	unsigned n_intra;                  /**< Intra-frames decoded      */
	unsigned n_picup;                  /**< Picture updates sent      */
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
	video_err_h *errh;
	void *arg;
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
	burst = (1 + jfs - prev_jfs) * bandwidth_kbps / 4;

	burst = min(burst, BURST_MAX);
	sent  = 0;

	while (le) {

		struct vidqent *qent = le->data;

		sent += mbuf_get_left(qent->mb);

		stream_send(vtx->video->strm, qent->marker, qent->pt,
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
	lock_write_get(vtx->lock);
	mem_deref(vtx->frame);
	mem_deref(vtx->mute_frame);
	mem_deref(vtx->enc);
	list_flush(&vtx->filtl);
	lock_rel(vtx->lock);
	mem_deref(vtx->lock);

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


static int get_fps(const struct video *v)
{
	const char *attr;

	/* RFC4566 */
	attr = sdp_media_rattr(stream_sdpmedia(v->strm), "framerate");
	if (attr) {
		/* NOTE: fractional values are ignored */
		const double fps = atof(attr);
		return (int)fps;
	}
	else
		return v->cfg.fps;
}


static int packet_handler(bool marker, const uint8_t *hdr, size_t hdr_len,
			  const uint8_t *pld, size_t pld_len, void *arg)
{
	struct vtx *vtx = arg;
	struct stream *strm = vtx->video->strm;
	struct vidqent *qent;
	int err;

	err = vidqent_alloc(&qent, marker, strm->pt_enc, vtx->ts_tx,
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
 * @param vtx   Video transmit object
 * @param frame Video frame to send
 */
static void encode_rtp_send(struct vtx *vtx, struct vidframe *frame)
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

	lock_write_get(vtx->lock);

	/* Convert image */
	if (frame->fmt != VIDENC_INTERNAL_FMT) {

		vtx->vsrc_size = frame->size;

		if (!vtx->frame) {

			err = vidframe_alloc(&vtx->frame, VIDENC_INTERNAL_FMT,
					     &vtx->vsrc_size);
			if (err)
				goto unlock;
		}

		vidconv(vtx->frame, frame, 0);
		frame = vtx->frame;
	}

	/* Process video frame through all Video Filters */
	for (le = vtx->filtl.head; le; le = le->next) {

		struct vidfilt_enc_st *st = le->data;

		if (st->vf && st->vf->ench)
			err |= st->vf->ench(st, frame);
	}

 unlock:
	lock_rel(vtx->lock);

	if (err)
		return;

	/* Encode the whole picture frame */
	err = vtx->vc->ench(vtx->enc, vtx->picup, frame);
	if (err)
		return;

	vtx->ts_tx += (SRATE/vtx->vsrc_prm.fps);
	vtx->picup = false;
}


/**
 * Read frames from video source
 *
 * @param frame Video frame
 * @param arg   Handler argument
 *
 * @note This function has REAL-TIME properties
 */
static void vidsrc_frame_handler(struct vidframe *frame, void *arg)
{
	struct vtx *vtx = arg;

	++vtx->frames;

	/* Is the video muted? If so insert video mute image */
	if (vtx->muted)
		frame = vtx->mute_frame;

	if (vtx->muted && vtx->muted_frames >= MAX_MUTED_FRAMES)
		return;

	/* Encode and send */
	encode_rtp_send(vtx, frame);
	vtx->muted_frames++;
}


static void vidsrc_error_handler(int err, void *arg)
{
	struct vtx *vtx = arg;

	warning("video: video-source error: %m\n", err);

	vtx->vsrc = mem_deref(vtx->vsrc);
}


static int vtx_alloc(struct vtx *vtx, struct video *video)
{
	int err;

	err = lock_alloc(&vtx->lock);
	err |= lock_alloc(&vtx->lock_tx);
	if (err)
		return err;

	tmr_init(&vtx->tmr_rtp);

	vtx->video = video;
	vtx->ts_tx = 160;

	str_ncpy(vtx->device, video->cfg.src_dev, sizeof(vtx->device));

	tmr_start(&vtx->tmr_rtp, 1, rtp_tmr_handler, vtx);

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

	return err;
}


static void picup_tmr_handler(void *arg)
{
	struct vrx *vrx = arg;

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

	/* XXX: if RTCP is not enabled, send XML in SIP INFO ? */

	++vrx->n_picup;
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
			err |= st->vf->dech(st, frame);
	}

	err = vidisp_display(vrx->vidisp, v->peer, frame);
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


static int pt_handler(struct video *v, uint8_t pt_old, uint8_t pt_new)
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
				struct mbuf *mb, void *arg)
{
	struct video *v = arg;
	int err;

	if (!mb)
		goto out;

	/* Video payload-type changed? */
	if (hdr->pt == v->vrx.pt_rx)
		goto out;

	err = pt_handler(v, v->vrx.pt_rx, hdr->pt);
	if (err)
		return;

 out:
	(void)video_stream_decode(&v->vrx, hdr, mb);
}


static void rtcp_handler(struct rtcp_msg *msg, void *arg)
{
	struct video *v = arg;

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


int video_alloc(struct video **vp, const struct config *cfg,
		struct call *call, struct sdp_session *sdp_sess, int label,
		const struct mnat *mnat, struct mnat_sess *mnat_sess,
		const struct menc *menc, struct menc_sess *menc_sess,
		const char *content, const struct list *vidcodecl,
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

	err = stream_alloc(&v->strm, &cfg->avt, call, sdp_sess, "video", label,
			   mnat, mnat_sess, menc, menc_sess,
			   call_localuri(call),
			   stream_recv_handler, rtcp_handler, v);
	if (err)
		goto out;

	if (cfg->avt.rtp_bw.max >= AUDIO_BANDWIDTH) {
		stream_set_bw(v->strm, cfg->avt.rtp_bw.max - AUDIO_BANDWIDTH);
	}

	err |= sdp_media_set_lattr(stream_sdpmedia(v->strm), true,
				   "framerate", "%d", v->cfg.fps);

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
	for (le = list_head(vidfilt_list()); le; le = le->next) {
		struct vidfilt *vf = le->data;
		void *ctx = NULL;

		err |= vidfilt_enc_append(&v->vtx.filtl, &ctx, vf);
		err |= vidfilt_dec_append(&v->vrx.filtl, &ctx, vf);
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

	info("video: display resized: %u x %u\n", sz->w, sz->h);

	/* XXX: update wanted picturesize and send re-invite to peer */
}


/* Set the video display - can be called multiple times */
static int set_vidisp(struct vrx *vrx)
{
	struct vidisp *vd;

	vrx->vidisp = mem_deref(vrx->vidisp);
	vrx->vidisp_prm.view = NULL;

	vd = (struct vidisp *)vidisp_find(vrx->video->cfg.disp_mod);
	if (!vd)
		return ENOENT;

	return vd->alloch(&vrx->vidisp, vd, &vrx->vidisp_prm, vrx->device,
			  vidisp_resize_handler, vrx);
}


/* Set the encoder format - can be called multiple times */
static int set_encoder_format(struct vtx *vtx, const char *src,
			      const char *dev, struct vidsz *size)
{
	struct vidsrc *vs = (struct vidsrc *)vidsrc_find(src);
	int err;

	if (!vs)
		return ENOENT;

	vtx->vsrc_size       = *size;
	vtx->vsrc_prm.fps    = get_fps(vtx->video);
	vtx->vsrc_prm.orient = VIDORIENT_PORTRAIT;

	vtx->vsrc = mem_deref(vtx->vsrc);

	err = vs->alloch(&vtx->vsrc, vs, NULL, &vtx->vsrc_prm,
			 &vtx->vsrc_size, NULL, dev, vidsrc_frame_handler,
			 vidsrc_error_handler, vtx);
	if (err) {
		info("video: no video source '%s': %m\n", src, err);
		return err;
	}

	vtx->mute_frame = mem_deref(vtx->mute_frame);
	err = vidframe_alloc(&vtx->mute_frame, VIDENC_INTERNAL_FMT, size);
	if (err)
		return err;

	vidframe_fill(vtx->mute_frame, 0xff, 0xff, 0xff);

	return err;
}


enum {TMR_INTERVAL = 5};
static void tmr_handler(void *arg)
{
	struct video *v = arg;

	tmr_start(&v->tmr, TMR_INTERVAL * 1000, tmr_handler, v);

	/* Estimate framerates */
	v->vtx.efps = v->vtx.frames / TMR_INTERVAL;
	v->vrx.efps = v->vrx.frames / TMR_INTERVAL;

	v->vtx.frames = 0;
	v->vrx.frames = 0;
}


int video_start(struct video *v, const char *peer)
{
	struct vidsz size;
	int err;

	if (!v)
		return EINVAL;

	if (peer) {
		mem_deref(v->peer);
		err = str_dup(&v->peer, peer);
		if (err)
			return err;
	}

	stream_set_srate(v->strm, SRATE, SRATE);

	err = set_vidisp(&v->vrx);
	if (err) {
		warning("video: could not set vidisp '%s': %m\n",
			v->vrx.device, err);
	}

	size.w = v->cfg.width;
	size.h = v->cfg.height;
	err = set_encoder_format(&v->vtx, v->cfg.src_mod,
				 v->vtx.device, &size);
	if (err) {
		warning("video: could not set encoder format to"
			" [%u x %u] %m\n",
			size.w, size.h, err);
	}

	tmr_start(&v->tmr, TMR_INTERVAL * 1000, tmr_handler, v);

	if (v->vtx.vc && v->vrx.vc) {
		info("%H%H",
		     vtx_print_pipeline, &v->vtx,
		     vrx_print_pipeline, &v->vrx);
	}

	return 0;
}


void video_stop(struct video *v)
{
	if (!v)
		return;

	v->vtx.vsrc = mem_deref(v->vtx.vsrc);
}


/**
 * Mute the video stream
 *
 * @param v     Video stream
 * @param muted True to mute, false to un-mute
 */
void video_mute(struct video *v, bool muted)
{
	struct vtx *vtx;

	if (!v)
		return;

	vtx = &v->vtx;

	vtx->muted        = muted;
	vtx->muted_frames = 0;
	vtx->picup        = true;

	video_update_picture(v);
}


static int vidisp_update(struct vrx *vrx)
{
	struct vidisp *vd = vidisp_get(vrx->vidisp);
	int err = 0;

	if (vd->updateh) {
		err = vd->updateh(vrx->vidisp, vrx->fullscreen,
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

	v->vrx.fullscreen = fs;

	return vidisp_update(&v->vrx);
}


static void vidsrc_update(struct vtx *vtx, const char *dev)
{
	struct vidsrc *vs = vidsrc_get(vtx->vsrc);

	if (vs && vs->updateh)
		vs->updateh(vtx->vsrc, &vtx->vsrc_prm, dev);
}


/**
 * Set the orientation of the Video source and display
 *
 * @param v      Video stream
 * @param orient Video orientation (enum vidorient)
 *
 * @return 0 if success, otherwise errorcode
 */
int video_set_orient(struct video *v, int orient)
{
	if (!v)
		return EINVAL;

	v->vtx.vsrc_prm.orient = v->vrx.orient = orient;
	vidsrc_update(&v->vtx, NULL);
	return vidisp_update(&v->vrx);
}


int video_encoder_set(struct video *v, struct vidcodec *vc,
		      int pt_tx, const char *params)
{
	struct vtx *vtx;
	int err = 0;

	if (!v)
		return EINVAL;

	vtx = &v->vtx;

	if (vc != vtx->vc) {

		struct videnc_param prm;

		prm.bitrate = v->cfg.bitrate;
		prm.pktsize = 1024;
		prm.fps     = get_fps(v);
		prm.max_fs  = -1;

		info("Set video encoder: %s %s (%u bit/s, %u fps)\n",
		     vc->name, vc->variant, prm.bitrate, prm.fps);

		vtx->enc = mem_deref(vtx->enc);
		err = vc->encupdh(&vtx->enc, vc, &prm, params,
				  packet_handler, vtx);
		if (err) {
			warning("video: encoder alloc: %m\n", err);
			return err;
		}

		vtx->vc = vc;
	}

	stream_update_encoder(v->strm, pt_tx);

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
		struct vidcodec *vcd;

		info("video: vidcodec '%s' has no decoder\n", vc->name);

		vcd = (struct vidcodec *)vidcodec_find_decoder(vc->name);
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
 * Use the next video encoder in the local list of negotiated codecs
 *
 * @param video  Video object
 */
void video_encoder_cycle(struct video *video)
{
	const struct sdp_format *rc = NULL;

	if (!video)
		return;

	rc = sdp_media_format_cycle(stream_sdpmedia(video_strm(video)));
	if (!rc) {
		info("cycle video: no remote codec found\n");
		return;
	}

	(void)video_encoder_set(video, rc->data, rc->pt, rc->params);
}


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
 * Get the driver-specific view of the video stream
 *
 * @param v Video stream
 *
 * @return Opaque view
 */
void *video_view(const struct video *v)
{
	if (!v)
		return NULL;

	return v->vrx.vidisp_prm.view;
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


static bool sdprattr_contains(struct stream *s, const char *name,
			      const char *str)
{
	const char *attr = sdp_media_rattr(stream_sdpmedia(s), name);
	return attr ? (NULL != strstr(attr, str)) : false;
}


void video_sdp_attr_decode(struct video *v)
{
	if (!v)
		return;

	/* RFC 4585 */
	v->nack_pli = sdprattr_contains(v->strm, "rtcp-fb", "nack");
}


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
	err |= re_hprintf(pf, " tx: %u x %u, fps=%d\n",
			  vtx->vsrc_size.w,
			  vtx->vsrc_size.h, vtx->vsrc_prm.fps);
	err |= re_hprintf(pf, "     skipc=%u\n", vtx->skipc);
	err |= re_hprintf(pf, " rx: pt=%d\n", vrx->pt_rx);
	err |= re_hprintf(pf, "     n_intra=%u, n_picup=%u\n",
			  vrx->n_intra, vrx->n_picup);

	if (!list_isempty(vidfilt_list())) {
		err |= vtx_print_pipeline(pf, vtx);
		err |= vrx_print_pipeline(pf, vrx);
	}

	err |= stream_debug(pf, v->strm);

	return err;
}


int video_print(struct re_printf *pf, const struct video *v)
{
	if (!v)
		return 0;

	return re_hprintf(pf, " efps=%d/%d", v->vtx.efps, v->vrx.efps);
}


int video_set_source(struct video *v, const char *name, const char *dev)
{
	struct vidsrc *vs = (struct vidsrc *)vidsrc_find(name);
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


void video_set_devicename(struct video *v, const char *src, const char *disp)
{
	if (!v)
		return;

	str_ncpy(v->vtx.device, src, sizeof(v->vtx.device));
	str_ncpy(v->vrx.device, disp, sizeof(v->vrx.device));
}
