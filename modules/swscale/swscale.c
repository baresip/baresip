/**
 * @file swscale.c  Video filter for scaling and pixel conversion
 *
 * Copyright (C) 2010 - 2016 Creytiv.com
 */
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <libswscale/swscale.h>


struct swscale_enc {
	struct vidfilt_enc_st vf;   /**< Inheritance           */

	struct SwsContext *sws;
	struct vidframe *frame;
	struct vidsz dst_size;
};


static enum vidfmt swscale_format = VID_FMT_YUV420P;  /* XXX: configurable */


static enum AVPixelFormat vidfmt_to_avpixfmt(enum vidfmt fmt)
{
	switch (fmt) {

	case VID_FMT_YUV420P: return AV_PIX_FMT_YUV420P;
	case VID_FMT_YUV444P: return AV_PIX_FMT_YUV444P;
	case VID_FMT_NV12:    return AV_PIX_FMT_NV12;
	case VID_FMT_NV21:    return AV_PIX_FMT_NV21;
	default:              return AV_PIX_FMT_NONE;
	}
}


static void encode_destructor(void *arg)
{
	struct swscale_enc *st = arg;

	list_unlink(&st->vf.le);

	mem_deref(st->frame);
	sws_freeContext(st->sws);
}


static int encode_update(struct vidfilt_enc_st **stp, void **ctx,
			 const struct vidfilt *vf)
{
	struct swscale_enc *st;
	struct config *config = conf_config();
	int err = 0;

	if (!config) {
		warning("swscale: no config\n");
		return EINVAL;
	}

	if (!stp || !ctx || !vf)
		return EINVAL;

	if (*stp)
		return 0;

	st = mem_zalloc(sizeof(*st), encode_destructor);
	if (!st)
		return ENOMEM;

	st->dst_size.w = config->video.width;
	st->dst_size.h = config->video.height;

	if (err)
		mem_deref(st);
	else
		*stp = (struct vidfilt_enc_st *)st;

	return err;
}


static int encode_process(struct vidfilt_enc_st *st, struct vidframe *frame,
			  uint64_t *timestamp)
{
	struct swscale_enc *enc = (struct swscale_enc *)st;
	enum AVPixelFormat avpixfmt, avpixfmt_dst;
	const uint8_t *srcSlice[4];
	uint8_t *dst[4];
	int srcStride[4], dstStride[4];
	int width, height, i, h;
	int err = 0;
	(void)timestamp;

	if (!st)
		return EINVAL;

	if (!frame)
		return 0;

	width = frame->size.w;
	height = frame->size.h;

	avpixfmt = vidfmt_to_avpixfmt(frame->fmt);
	if (avpixfmt == AV_PIX_FMT_NONE) {
		warning("swscale: unknown pixel-format (%s)\n",
			vidfmt_name(frame->fmt));
		return EINVAL;
	}

	avpixfmt_dst = vidfmt_to_avpixfmt(swscale_format);
	if (avpixfmt_dst == AV_PIX_FMT_NONE) {
		warning("swscale: unknown pixel-format (%s)\n",
			vidfmt_name(swscale_format));
		return EINVAL;
	}

	if (!enc->sws) {

		struct SwsContext *sws;
		int flags = 0;

		sws = sws_getContext(width, height, avpixfmt,
				     enc->dst_size.w, enc->dst_size.h,
				     avpixfmt_dst,
				     flags, NULL, NULL, NULL);
		if (!sws) {
			warning("swscale: sws_getContext error\n");
			return ENOMEM;
		}

		enc->sws = sws;

		info("swscale: created SwsContext:"
		     " `%s' %d x %d --> `%s' %u x %u\n",
		     vidfmt_name(frame->fmt), width, height,
		     vidfmt_name(swscale_format),
		     enc->dst_size.w, enc->dst_size.h);
	}

	if (!enc->frame) {

		err = vidframe_alloc(&enc->frame, swscale_format,
				     &enc->dst_size);
		if (err) {
			warning("swscale: vidframe_alloc error (%m)\n", err);
			return err;
		}
	}

	for (i=0; i<4; i++) {
		srcSlice[i]  = frame->data[i];
		srcStride[i] = frame->linesize[i];
		dst[i]       = enc->frame->data[i];
		dstStride[i] = enc->frame->linesize[i];
	}

	h = sws_scale(enc->sws, srcSlice, srcStride,
		      0, height, dst, dstStride);
	if (h <= 0) {
		warning("swscale: sws_scale error (%d)\n", h);
		return EPROTO;
	}

	/* Copy the converted frame back to the input frame */
	for (i=0; i<4; i++) {
		frame->data[i]     = enc->frame->data[i];
		frame->linesize[i] = enc->frame->linesize[i];
	}
	frame->size = enc->frame->size;
	frame->fmt = enc->frame->fmt;

	return 0;
}


static struct vidfilt vf_swscale = {
	LE_INIT, "swscale", encode_update, encode_process, NULL, NULL
};


static int module_init(void)
{
	vidfilt_register(baresip_vidfiltl(), &vf_swscale);
	return 0;
}


static int module_close(void)
{
	vidfilt_unregister(&vf_swscale);
	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(swscale) = {
	"swscale",
	"vidfilt",
	module_init,
	module_close
};
