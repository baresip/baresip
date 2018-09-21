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
  baresip -e/vidloop
 \endverbatim
 *
 * Example usage with codec:
 \verbatim
  baresip -e"/vidloop h264"
 \endverbatim
 */


/** Video Statistics */
struct vstat {
	uint64_t tsamp;
	uint32_t frames;
	size_t bytes;
	uint32_t bitrate;
	double efps;
	size_t n_intra;
};


struct timestamp_state {
	uint64_t base;  /* lowest timestamp */
	uint64_t last;  /* most recent timestamp */
	bool is_set;
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
	struct vidsrc_prm srcprm;
	struct list filtencl;
	struct list filtdecl;
	struct vstat stat;
	struct tmr tmr_bw;
	struct tmr tmr_display;
	struct vidsz src_size;
	struct vidsz disp_size;
	enum vidfmt src_fmt;
	struct vidframe *frame;
	uint64_t frame_timestamp;
	struct lock *frame_mutex;
	bool new_frame;
	uint64_t ts_start;      /* usec */
	uint64_t ts_last;       /* usec */
	uint16_t seq;
	bool need_conv;
	bool started;
	int err;

	struct {
		uint64_t src_frames;
		uint64_t enc_bytes;
		uint64_t enc_packets;
		uint64_t disp_frames;
	} stats;

	struct timestamp_state ts_src;
	struct timestamp_state ts_rtp;
};


static struct video_loop *gvl;


static void timestamp_state_update(struct timestamp_state *st,
				   uint64_t ts)
{
	if (st->is_set) {
		if (ts < st->base) {
			warning("vidloop: timestamp wrapped -- reset base"
				" (base=%llu, current=%llu)\n",
				st->base, ts);
			st->base = ts;
		}
	}
	else {
		st->base = ts;
		st->is_set = true;
	}

	st->last = ts;
}


static double timestamp_state_duration(const struct timestamp_state *ts,
				       uint32_t clock)
{
	uint64_t dur;

	if (ts->is_set)
		dur = ts->last - ts->base;
	else
		dur = 0;

	return (double)dur / (double)clock;
}


static void display_handler(void *arg)
{
	struct video_loop *vl = arg;
	int err;

	tmr_start(&vl->tmr_display, 10, display_handler, vl);

	lock_write_get(vl->frame_mutex);

	if (!vl->new_frame)
		goto out;

	/* display frame */
	err = vidisp_display(vl->vidisp, "Video Loop",
			     vl->frame, vl->frame_timestamp);
	vl->new_frame = false;

	if (err == ENODEV) {
		info("vidloop: video-display was closed\n");
		vl->vidisp = mem_deref(vl->vidisp);
	}

 out:
	lock_rel(vl->frame_mutex);
}


static int display(struct video_loop *vl, struct vidframe *frame,
		   uint64_t timestamp)
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
			err |= st->vf->dech(st, frame, &timestamp);
	}

	if (err) {
		warning("vidloop: error in decode video-filter (%m)\n", err);
	}

	/* save the displayed frame info */
	vl->disp_size = frame->size;
	++vl->stats.disp_frames;

	lock_write_get(vl->frame_mutex);

	if (vl->frame && ! vidsz_cmp(&vl->frame->size, &frame->size)) {

		info("vidloop: resolution changed:  %u x %u\n",
		     frame->size.w, frame->size.h);

		vl->frame = mem_deref(vl->frame);
	}

	if (!vl->frame) {
		err = vidframe_alloc(&vl->frame, frame->fmt, &frame->size);
		if (err)
			goto out;
	}

	vidframe_copy(vl->frame, frame);
	vl->frame_timestamp = timestamp;
	vl->new_frame = true;

 out:
	lock_rel(vl->frame_mutex);

	mem_deref(frame_filt);

	return err;
}


static int packet_handler(bool marker, uint64_t rtp_ts,
			  const uint8_t *hdr, size_t hdr_len,
			  const uint8_t *pld, size_t pld_len,
			  void *arg)
{
	struct video_loop *vl = arg;
	struct vidframe frame;
	struct mbuf *mb;
	uint64_t timestamp;
	bool intra;
	int err = 0;

	++vl->stats.enc_packets;
	vl->stats.enc_bytes += (hdr_len + pld_len);

	timestamp_state_update(&vl->ts_rtp, rtp_ts);

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

	/* convert the RTP timestamp to VIDEO_TIMEBASE timestamp */
	timestamp = video_calc_timebase_timestamp(rtp_ts);

	if (vidframe_isvalid(&frame)) {

		display(vl, &frame, timestamp);
	}

 out:
	mem_deref(mb);

	return 0;
}


static void vidsrc_frame_handler(struct vidframe *frame, uint64_t timestamp,
				 void *arg)
{
	struct video_loop *vl = arg;
	struct vidframe *f2 = NULL;
	struct le *le;
	const uint64_t now = tmr_jiffies_usec();
	int err = 0;

	/* save the timing info */
	if (!gvl->ts_start)
		gvl->ts_start = now;
	gvl->ts_last = now;

	/* save the video frame info */
	vl->src_size = frame->size;
	vl->src_fmt = frame->fmt;
	++vl->stats.src_frames;

	timestamp_state_update(&vl->ts_src, timestamp);

	++vl->stat.frames;

	if (frame->fmt != (enum vidfmt)vl->cfg.enc_fmt) {

		if (!vl->need_conv) {
			info("vidloop: NOTE: pixel-format conversion"
			     " needed: %s  -->  %s\n",
			     vidfmt_name(frame->fmt),
			     vidfmt_name(vl->cfg.enc_fmt));
			vl->need_conv = true;
		}

		if (vidframe_alloc(&f2, vl->cfg.enc_fmt, &frame->size))
			return;

		vidconv(f2, frame, 0);

		frame = f2;
	}

	/* Process video frame through all Video Filters */
	for (le = vl->filtencl.head; le; le = le->next) {

		struct vidfilt_enc_st *st = le->data;

		if (st->vf->ench)
			err |= st->vf->ench(st, frame, &timestamp);
	}

	if (vl->vc_enc && vl->enc) {

		err = vl->vc_enc->ench(vl->enc, false, frame, timestamp);
		if (err) {
			warning("vidloop: encoder error (%m)\n", err);
			goto out;
		}
	}
	else {
		vl->stat.bytes += vidframe_size(frame->fmt, &frame->size);
		(void)display(vl, frame, timestamp);
	}

 out:
	mem_deref(f2);
}


static int print_stats(struct re_printf *pf, const struct video_loop *vl)
{
	const struct config_video *cfg = &vl->cfg;
	double src_dur, real_dur = .0;
	int err = 0;

	src_dur = timestamp_state_duration(&vl->ts_src, VIDEO_TIMEBASE);

	if (vl->ts_start)
		real_dur = (vl->ts_last - vl->ts_start) * .000001;

	err |= re_hprintf(pf, "~~~~~ Videoloop summary: ~~~~~\n");

	/* Source */
	if (vl->vsrc) {
		struct vidsrc *vs = vidsrc_get(vl->vsrc);
		double avg_fps = .0;

		if (vl->stats.src_frames >= 2)
			avg_fps = (vl->stats.src_frames-1) / src_dur;

		err |= re_hprintf(pf,
				  "* Source\n"
				  "  module      %s\n"
				  "  resolution  %u x %u (actual %u x %u)\n"
				  "  pixformat   %s\n"
				  "  frames      %llu\n"
				  "  framerate   %.2f fps  (avg %.2f fps)\n"
				  "  duration    %.3f sec  (real %.3f sec)\n"
				  "\n"
				  ,
				  vs->name,
				  cfg->width, cfg->height,
				  vl->src_size.w, vl->src_size.h,
				  vidfmt_name(vl->src_fmt),
				  vl->stats.src_frames,
				  vl->srcprm.fps, avg_fps,
				  src_dur, real_dur);
	}

	/* Video conversion */
	if (vl->need_conv) {
		err |= re_hprintf(pf,
				  "* Vidconv\n"
				  "  pixformat   %s\n"
				  "\n"
				  ,
				  vidfmt_name(cfg->enc_fmt));
	}

	/* Filters */
	if (!list_isempty(baresip_vidfiltl())) {
		struct le *le;

		err |= re_hprintf(pf,
				  "* Filters (%u):",
				  list_count(baresip_vidfiltl()));

		for (le = list_head(baresip_vidfiltl()); le; le = le->next) {
			struct vidfilt *vf = le->data;
			err |= re_hprintf(pf, " %s", vf->name);
		}
		err |= re_hprintf(pf, "\n\n");
	}

	/* Encoder */
	if (vl->vc_enc) {
		double avg_bitrate;
		double avg_pktrate;
		double dur;

		avg_bitrate = 8.0 * (double)vl->stats.enc_bytes / src_dur;
		avg_pktrate = (double)vl->stats.enc_packets / src_dur;
		dur = timestamp_state_duration(&vl->ts_rtp, 90000);

		err |= re_hprintf(pf,
				  "* Encoder\n"
				  "  module      %s\n"
				  "  bitrate     %u bit/s (avg %.1f bit/s)\n"
				  "  packets     %llu     (avg %.1f pkt/s)\n"
				  "  duration    %.3f sec\n"
				  "\n"
				  ,
				  vl->vc_enc->name,
				  cfg->bitrate, avg_bitrate,
				  vl->stats.enc_packets, avg_pktrate,
				  dur);
	}

	/* Decoder */
	if (vl->vc_dec) {
		err |= re_hprintf(pf,
				  "* Decoder\n"
				  "  module      %s\n"
				  "  key-frames  %zu\n"
				  "\n"
				  ,
				  vl->vc_dec->name,
				  vl->stat.n_intra);
	}

	/* Display */
	if (vl->vidisp) {
		struct vidisp *vd = vidisp_get(vl->vidisp);

		err |= re_hprintf(pf,
				  "* Display\n"
				  "  module      %s\n"
				  "  resolution  %u x %u\n"
				  "  fullscreen  %s\n"
				  "  frames      %llu\n"
				  "\n"
				  ,
				  vd->name,
				  vl->disp_size.w, vl->disp_size.h,
				  cfg->fullscreen ? "Yes" : "No",
				  vl->stats.disp_frames);
	}

	return err;
}


static void vidloop_destructor(void *arg)
{
	struct video_loop *vl = arg;

	if (vl->started)
		re_printf("%H\n", print_stats, vl);

	tmr_cancel(&vl->tmr_bw);
	mem_deref(vl->vsrc);
	mem_deref(vl->enc);
	mem_deref(vl->dec);

	lock_write_get(vl->frame_mutex);
	mem_deref(vl->vidisp);
	mem_deref(vl->frame);
	tmr_cancel(&vl->tmr_display);
	lock_rel(vl->frame_mutex);

	list_flush(&vl->filtencl);
	list_flush(&vl->filtdecl);
	mem_deref(vl->frame_mutex);
}


static int enable_codec(struct video_loop *vl, const char *name)
{
	struct list *vidcodecl = baresip_vidcodecl();
	struct videnc_param prm;
	int err;

	prm.fps     = vl->cfg.fps;
	prm.pktsize = 1480;
	prm.bitrate = vl->cfg.bitrate;
	prm.max_fs  = -1;

	/* Use the first video codec */

	vl->vc_enc = vidcodec_find_encoder(vidcodecl, name);
	if (!vl->vc_enc) {
		warning("vidloop: could not find encoder (%s)\n", name);
		return ENOENT;
	}

	info("vidloop: enabled encoder %s (%.2f fps, %u bit/s)\n",
	     vl->vc_enc->name, prm.fps, prm.bitrate);

	vl->vc_dec = vidcodec_find_decoder(vidcodecl, name);
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


static void print_status(struct video_loop *vl)
{
	(void)re_fprintf(stdout,
			 "\rstatus:"
			 " %.3f sec [%s] [%s]  fmt=%s  intra=%zu "
			 " EFPS=%.1f      %u kbit/s       \r",
			 timestamp_state_duration(&vl->ts_src,
						  VIDEO_TIMEBASE),

			 vl->vc_enc ? vl->vc_enc->name : "",
			 vl->vc_dec ? vl->vc_dec->name : "",
			 vidfmt_name(vl->cfg.enc_fmt),
			 vl->stat.n_intra,
			 vl->stat.efps, vl->stat.bitrate);
	fflush(stdout);
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

	tmr_start(&vl->tmr_bw, 500, timeout_bw, vl);

	calc_bitrate(vl);
	print_status(vl);
}


static int vsrc_reopen(struct video_loop *vl, const struct vidsz *sz)
{
	int err;

	info("vidloop: %s,%s: open video source: %u x %u at %.2f fps\n",
	     vl->cfg.src_mod, vl->cfg.src_dev,
	     sz->w, sz->h, vl->cfg.fps);

	vl->srcprm.orient = VIDORIENT_PORTRAIT;
	vl->srcprm.fps    = vl->cfg.fps;

	vl->vsrc = mem_deref(vl->vsrc);
	err = vidsrc_alloc(&vl->vsrc, baresip_vidsrcl(),
			   vl->cfg.src_mod, NULL, &vl->srcprm, sz,
			   NULL, vl->cfg.src_dev, vidsrc_frame_handler,
			   NULL, vl);
	if (err) {
		warning("vidloop: vidsrc '%s' failed: %m\n",
			vl->cfg.src_dev, err);
	}

	return err;
}


static int video_loop_alloc(struct video_loop **vlp)
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
	tmr_init(&vl->tmr_display);

	err = lock_alloc(&vl->frame_mutex);
	if (err)
		goto out;

	vl->new_frame = false;
	vl->frame = NULL;

	/* Video filters */
	for (le = list_head(baresip_vidfiltl()); le; le = le->next) {
		struct vidfilt *vf = le->data;
		void *ctx = NULL;

		info("vidloop: added video-filter `%s'\n", vf->name);

		err |= vidfilt_enc_append(&vl->filtencl, &ctx, vf);
		err |= vidfilt_dec_append(&vl->filtdecl, &ctx, vf);
		if (err) {
			warning("vidloop: vidfilt error: %m\n", err);
		}
	}

	info("vidloop: open video display (%s.%s)\n",
	     vl->cfg.disp_mod, vl->cfg.disp_dev);

	err = vidisp_alloc(&vl->vidisp, baresip_vidispl(),
			   vl->cfg.disp_mod, NULL,
			   vl->cfg.disp_dev, NULL, vl);
	if (err) {
		warning("vidloop: video display failed: %m\n", err);
		goto out;
	}

	tmr_start(&vl->tmr_bw, 1000, timeout_bw, vl);

	/* NOTE: usually (e.g. SDL2),
			 video frame must be rendered from main thread */
	tmr_start(&vl->tmr_display, 10, display_handler, vl);

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
	const struct cmd_arg *carg = arg;
	struct vidsz size;
	struct config *cfg = conf_config();
	const char *codec_name = carg->prm;
	int err = 0;

	size.w = cfg->video.width;
	size.h = cfg->video.height;

	if (gvl) {
		return re_hprintf(pf, "video-loop already running.\n");
	}

	(void)re_hprintf(pf, "Enable video-loop on %s,%s: %u x %u\n",
			 cfg->video.src_mod, cfg->video.src_dev,
			 size.w, size.h);

	err = video_loop_alloc(&gvl);
	if (err) {
		warning("vidloop: alloc: %m\n", err);
		return err;
	}

	if (str_isset(codec_name)) {

		err = enable_codec(gvl, codec_name);
		if (err) {
			gvl = mem_deref(gvl);
			return err;
		}

		(void)re_hprintf(pf, "%sabled codec: %s\n",
				 gvl->vc_enc ? "En" : "Dis",
				 gvl->vc_enc ? gvl->vc_enc->name : "");
	}

	/* Start video source, after codecs are created */
	err = vsrc_reopen(gvl, &size);
	if (err) {
		gvl = mem_deref(gvl);
		return err;
	}

	gvl->started = true;

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
	{"vidloop",     0, CMD_PRM, "Start video-loop <codec>", vidloop_start},
	{"vidloop_stop",0, 0,       "Stop video-loop",          vidloop_stop },
};


static int module_init(void)
{
	return cmd_register(baresip_commands(), cmdv, ARRAY_SIZE(cmdv));
}


static int module_close(void)
{
	gvl = mem_deref(gvl);
	cmd_unregister(baresip_commands(), cmdv);
	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(vidloop) = {
	"vidloop",
	"application",
	module_init,
	module_close,
};
