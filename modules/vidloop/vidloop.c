/**
 * @file vidloop.c  Video loop
 *
 * Copyright (C) 2010 Creytiv.com
 */
#define _DEFAULT_SOURCE 1
#define _BSD_SOURCE 1
#include <string.h>
#include <time.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>


/**
 * @defgroup vidloop vidloop
 *
 * A video-loop module for testing
 *
 * Simple test module that loops back the video frames from a
 * video-source to a video-display, optionally via a video codec.
 *
 * Example usage without codec:
 \verbatim
  baresip -ev
 \endverbatim
 *
 * Example usage with codec:
 \verbatim
  baresip -evv
 \endverbatim
 */


/** Internal pixel-format */
#ifndef VIDLOOP_INTERNAL_FMT
#define VIDLOOP_INTERNAL_FMT (VID_FMT_YUV420P)
#endif


/** Video Statistics */
struct vstat {
	uint64_t tsamp;
	uint32_t frames;
	size_t bytes;
	uint32_t bitrate;
	double efps;
	size_t n_intra;
};


/** Video loop */
struct video_loop {
	const struct vidcodec *vc_enc;
	const struct vidcodec *vc_dec;
	struct config_video cfg;
	struct videnc_state *enc;
	struct viddec_state *dec;
	struct vidisp_st *vidisp;
	struct vidsrc_st *vsrc;
	struct list filtencl;
	struct list filtdecl;
	struct vstat stat;
	struct tmr tmr_bw;
	uint16_t seq;
	bool need_conv;
	int err;
};


static struct video_loop *gvl;


static int display(struct video_loop *vl, struct vidframe *frame)
{
	struct vidframe *frame_filt = NULL;
	struct le *le;
	int err = 0;

	if (!vidframe_isvalid(frame))
		return 0;

	/* Process video frame through all Video Filters */
	for (le = vl->filtdecl.head; le; le = le->next) {

		struct vidfilt_dec_st *st = le->data;

		/* Some video decoders keeps the displayed video frame
		 * in memory and we should not write to that frame.
		 */
		if (!frame_filt) {

			err = vidframe_alloc(&frame_filt, frame->fmt,
					     &frame->size);
			if (err)
				return err;

			vidframe_copy(frame_filt, frame);

			frame = frame_filt;
		}

		if (st->vf->dech)
			err |= st->vf->dech(st, frame);
	}

	if (err) {
		warning("vidloop: error in video-filters (%m)\n", err);
	}

	/* display frame */
	err = vidisp_display(vl->vidisp, "Video Loop", frame);
	if (err == ENODEV) {
		info("vidloop: video-display was closed\n");
		vl->vidisp = mem_deref(vl->vidisp);
		vl->err = err;
	}

	mem_deref(frame_filt);

	return err;
}


static int packet_handler(bool marker, const uint8_t *hdr, size_t hdr_len,
			  const uint8_t *pld, size_t pld_len, void *arg)
{
	struct video_loop *vl = arg;
	struct vidframe frame;
	struct mbuf *mb;
	bool intra;
	int err = 0;

	mb = mbuf_alloc(hdr_len + pld_len);
	if (!mb)
		return ENOMEM;

	if (hdr_len)
		mbuf_write_mem(mb, hdr, hdr_len);
	mbuf_write_mem(mb, pld, pld_len);

	mb->pos = 0;

	vl->stat.bytes += mbuf_get_left(mb);

	/* decode */
	frame.data[0] = NULL;
	if (vl->vc_dec && vl->dec) {
		err = vl->vc_dec->dech(vl->dec, &frame, &intra,
				       marker, vl->seq++, mb);
		if (err) {
			warning("vidloop: codec decode: %m\n", err);
			goto out;
		}

		if (intra)
			++vl->stat.n_intra;
	}

	if (vidframe_isvalid(&frame)) {

		display(vl, &frame);
	}

 out:
	mem_deref(mb);

	return 0;
}


static void vidsrc_frame_handler(struct vidframe *frame, void *arg)
{
	struct video_loop *vl = arg;
	struct vidframe *f2 = NULL;
	struct le *le;
	int err = 0;

	++vl->stat.frames;

	if (frame->fmt != VIDLOOP_INTERNAL_FMT) {

		if (!vl->need_conv) {
			info("vidloop: NOTE: pixel-format conversion"
			     " needed: %s  -->  %s\n",
			     vidfmt_name(frame->fmt),
			     vidfmt_name(VIDLOOP_INTERNAL_FMT));
			vl->need_conv = true;
		}

		if (vidframe_alloc(&f2, VIDLOOP_INTERNAL_FMT, &frame->size))
			return;

		vidconv(f2, frame, 0);

		frame = f2;
	}

	/* Process video frame through all Video Filters */
	for (le = vl->filtencl.head; le; le = le->next) {

		struct vidfilt_enc_st *st = le->data;

		if (st->vf->ench)
			err |= st->vf->ench(st, frame);
	}

	if (vl->vc_enc && vl->enc) {
		(void)vl->vc_enc->ench(vl->enc, false, frame);
	}
	else {
		vl->stat.bytes += vidframe_size(frame->fmt, &frame->size);
		(void)display(vl, frame);
	}

	mem_deref(f2);
}


static void vidloop_destructor(void *arg)
{
	struct video_loop *vl = arg;

	tmr_cancel(&vl->tmr_bw);
	mem_deref(vl->vsrc);
	mem_deref(vl->enc);
	mem_deref(vl->dec);
	mem_deref(vl->vidisp);
	list_flush(&vl->filtencl);
	list_flush(&vl->filtdecl);
}


static int enable_codec(struct video_loop *vl)
{
	struct videnc_param prm;
	const char *name = NULL;
	int err;

	prm.fps     = vl->cfg.fps;
	prm.pktsize = 1480;
	prm.bitrate = vl->cfg.bitrate;
	prm.max_fs  = -1;

	/* Use the first video codec */

	vl->vc_enc = vidcodec_find_encoder(name);
	if (!vl->vc_enc) {
		warning("vidloop: could not find encoder (%s)\n", name);
		return ENOENT;
	}

	info("vidloop: enabled encoder %s (%u fps, %u bit/s)\n",
	     vl->vc_enc->name, prm.fps, prm.bitrate);

	vl->vc_dec = vidcodec_find_decoder(name);
	if (!vl->vc_dec) {
		warning("vidloop: could not find decoder (%s)\n", name);
		return ENOENT;
	}

	info("vidloop: enabled decoder %s\n", vl->vc_dec->name);

	err = vl->vc_enc->encupdh(&vl->enc, vl->vc_enc, &prm, NULL,
				  packet_handler, vl);
	if (err) {
		warning("vidloop: update encoder failed: %m\n", err);
		return err;
	}

	if (vl->vc_dec->decupdh) {
		err = vl->vc_dec->decupdh(&vl->dec, vl->vc_dec, NULL);
		if (err) {
			warning("vidloop: update decoder failed: %m\n", err);
			return err;
		}
	}

	return 0;
}


static void disable_codec(struct video_loop *vl)
{
	vl->enc = mem_deref(vl->enc);
	vl->dec = mem_deref(vl->dec);
	vl->vc_enc = NULL;
	vl->vc_dec = NULL;
}


static void print_status(struct video_loop *vl)
{
	(void)re_fprintf(stderr,
			 "\rstatus:"
			 " [%s] [%s]  intra=%zu "
			 " EFPS=%.1f      %u kbit/s       \r",
			 vl->vc_enc ? vl->vc_enc->name : "",
			 vl->vc_dec ? vl->vc_dec->name : "",
			 vl->stat.n_intra,
			 vl->stat.efps, vl->stat.bitrate);
}


static void calc_bitrate(struct video_loop *vl)
{
	const uint64_t now = tmr_jiffies();

	if (now > vl->stat.tsamp) {

		const uint32_t dur = (uint32_t)(now - vl->stat.tsamp);

		vl->stat.efps = 1000.0f * vl->stat.frames / dur;

		vl->stat.bitrate = (uint32_t) (8 * vl->stat.bytes / dur);
	}

	vl->stat.frames = 0;
	vl->stat.bytes = 0;
	vl->stat.tsamp = now;
}


static void timeout_bw(void *arg)
{
	struct video_loop *vl = arg;

	if (vl->err) {
		info("error in video-loop -- closing (%m)\n", vl->err);
		gvl = mem_deref(gvl);
		return;
	}

	tmr_start(&vl->tmr_bw, 2000, timeout_bw, vl);

	calc_bitrate(vl);
	print_status(vl);
}


static int vsrc_reopen(struct video_loop *vl, const struct vidsz *sz)
{
	struct vidsrc_prm prm;
	int err;

	info("vidloop: %s,%s: open video source: %u x %u at %u fps\n",
	     vl->cfg.src_mod, vl->cfg.src_dev,
	     sz->w, sz->h, vl->cfg.fps);

	prm.orient = VIDORIENT_PORTRAIT;
	prm.fps    = vl->cfg.fps;

	vl->vsrc = mem_deref(vl->vsrc);
	err = vidsrc_alloc(&vl->vsrc, vl->cfg.src_mod, NULL, &prm, sz,
			   NULL, vl->cfg.src_dev, vidsrc_frame_handler,
			   NULL, vl);
	if (err) {
		warning("vidloop: vidsrc '%s' failed: %m\n",
			vl->cfg.src_dev, err);
	}

	return err;
}


static int video_loop_alloc(struct video_loop **vlp, const struct vidsz *size)
{
	struct video_loop *vl;
	struct config *cfg;
	struct le *le;
	int err = 0;

	cfg = conf_config();
	if (!cfg)
		return EINVAL;

	vl = mem_zalloc(sizeof(*vl), vidloop_destructor);
	if (!vl)
		return ENOMEM;

	vl->cfg = cfg->video;
	tmr_init(&vl->tmr_bw);

	/* Video filters */
	for (le = list_head(vidfilt_list()); le; le = le->next) {
		struct vidfilt *vf = le->data;
		void *ctx = NULL;

		info("vidloop: added video-filter `%s'\n", vf->name);

		err |= vidfilt_enc_append(&vl->filtencl, &ctx, vf);
		err |= vidfilt_dec_append(&vl->filtdecl, &ctx, vf);
		if (err) {
			warning("vidloop: vidfilt error: %m\n", err);
		}
	}

	err = vsrc_reopen(vl, size);
	if (err)
		goto out;

	info("vidloop: open video display (%s.%s)\n",
	     vl->cfg.disp_mod, vl->cfg.disp_dev);

	err = vidisp_alloc(&vl->vidisp, vl->cfg.disp_mod, NULL,
			   vl->cfg.disp_dev, NULL, vl);
	if (err) {
		warning("vidloop: video display failed: %m\n", err);
		goto out;
	}

	tmr_start(&vl->tmr_bw, 1000, timeout_bw, vl);

 out:
	if (err)
		mem_deref(vl);
	else
		*vlp = vl;

	return err;
}


/**
 * Start the video loop (for testing)
 */
static int vidloop_start(struct re_printf *pf, void *arg)
{
	struct vidsz size;
	struct config *cfg = conf_config();
	int err = 0;

	(void)arg;

	size.w = cfg->video.width;
	size.h = cfg->video.height;

	if (gvl) {
		if (gvl->vc_enc)
			disable_codec(gvl);
		else {
			err = enable_codec(gvl);
			if (err) {
				gvl = mem_deref(gvl);
				return err;
			}
		}

		(void)re_hprintf(pf, "%sabled codec: %s\n",
				 gvl->vc_enc ? "En" : "Dis",
				 gvl->vc_enc ? gvl->vc_enc->name : "");
	}
	else {
		(void)re_hprintf(pf, "Enable video-loop on %s,%s: %u x %u\n",
				 cfg->video.src_mod, cfg->video.src_dev,
				 size.w, size.h);

		err = video_loop_alloc(&gvl, &size);
		if (err) {
			warning("vidloop: alloc: %m\n", err);
		}
	}

	return err;
}


static int vidloop_stop(struct re_printf *pf, void *arg)
{
	(void)arg;

	if (gvl)
		(void)re_hprintf(pf, "Disable video-loop\n");
	gvl = mem_deref(gvl);
	return 0;
}


static const struct cmd cmdv[] = {
	{"vidloop",      'v',  0, "Start video-loop", vidloop_start },
	{"vidloop_stop", 'V',  0, "Stop video-loop",  vidloop_stop  },
};


static int module_init(void)
{
	return cmd_register(baresip_commands(), cmdv, ARRAY_SIZE(cmdv));
}


static int module_close(void)
{
	vidloop_stop(NULL, NULL);
	cmd_unregister(baresip_commands(), cmdv);
	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(vidloop) = {
	"vidloop",
	"application",
	module_init,
	module_close,
};
