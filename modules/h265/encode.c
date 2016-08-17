/**
 * @file h265/encode.c H.265 Encode
 *
 * Copyright (C) 2010 Creytiv.com
 */

#include <string.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <x265.h>
#include "h265.h"


struct videnc_state {
	struct vidsz size;
	x265_param *param;
	x265_encoder *x265;
	int64_t pts;
	unsigned fps;
	unsigned bitrate;
	unsigned pktsize;
	videnc_packet_h *pkth;
	void *arg;
};


static void destructor(void *arg)
{
	struct videnc_state *st = arg;

	if (st->x265)
		x265_encoder_close(st->x265);
	if (st->param)
		x265_param_free(st->param);
}


static int set_params(struct videnc_state *st, unsigned fps, unsigned bitrate)
{
	st->param = x265_param_alloc();
	if (!st->param) {
		warning("h265: x265_param_alloc failed\n");
		return ENOMEM;
	}

	x265_param_default(st->param);

	if (0 != x265_param_apply_profile(st->param, "main")) {
		warning("h265: x265_param_apply_profile failed\n");
		return EINVAL;
	}

	if (0 != x265_param_default_preset(st->param,
					   "ultrafast", "zerolatency")) {

		warning("h265: x265_param_default_preset error\n");
		return EINVAL;
	}

	st->param->fpsNum = fps;
	st->param->fpsDenom = 1;

	/* VPS, SPS and PPS headers should be output with each keyframe */
	st->param->bRepeatHeaders = 1;

	/* Rate Control */
	st->param->rc.rateControlMode = X265_RC_CRF;
	st->param->rc.bitrate = bitrate / 1000;
	st->param->rc.vbvMaxBitrate = bitrate / 1000;
	st->param->rc.vbvBufferSize = 2 * bitrate / fps;

	return 0;
}


int h265_encode_update(struct videnc_state **vesp, const struct vidcodec *vc,
		       struct videnc_param *prm, const char *fmtp,
		       videnc_packet_h *pkth, void *arg)
{
	struct videnc_state *ves;
	int err = 0;
	(void)fmtp;

	if (!vesp || !vc || !prm || prm->pktsize < 3 || !pkth)
		return EINVAL;

	ves = *vesp;

	if (!ves) {

		ves = mem_zalloc(sizeof(*ves), destructor);
		if (!ves)
			return ENOMEM;

		*vesp = ves;
	}
	else {
		if (ves->x265 && (ves->bitrate != prm->bitrate ||
				  ves->pktsize != prm->pktsize ||
				  ves->fps     != prm->fps)) {

			x265_encoder_close(ves->x265);
			ves->x265 = NULL;
		}
	}

	ves->bitrate = prm->bitrate;
	ves->pktsize = prm->pktsize;
	ves->fps     = prm->fps;
	ves->pkth    = pkth;
	ves->arg     = arg;

	err = set_params(ves, prm->fps, prm->bitrate);
	if (err)
		return err;

	return 0;
}


static int open_encoder(struct videnc_state *st, const struct vidsz *size)
{
	if (st->x265) {
		debug("h265: re-opening encoder\n");
		x265_encoder_close(st->x265);
	}

	st->param->sourceWidth  = size->w;
	st->param->sourceHeight = size->h;

	st->x265 = x265_encoder_open(st->param);
	if (!st->x265) {
		warning("h265: x265_encoder_open failed\n");
		return ENOMEM;
	}

	return 0;
}


static inline int packetize(bool marker, const uint8_t *buf, size_t len,
			    size_t maxlen, videnc_packet_h *pkth, void *arg)
{
	int err = 0;

	if (len <= maxlen) {
		err = pkth(marker, NULL, 0, buf, len, arg);
	}
	else {
		struct h265_nal nal;
		uint8_t fu_hdr[3];
		const size_t flen = maxlen - sizeof(fu_hdr);

		err = h265_nal_decode(&nal, buf);

		h265_nal_encode(fu_hdr, H265_NAL_FU,
				nal.nuh_temporal_id_plus1);

		fu_hdr[2] = 1<<7 | nal.nal_unit_type;

		buf+=2;
		len-=2;

		while (len > flen) {
			err |= pkth(false, fu_hdr, 3, buf, flen, arg);

			buf += flen;
			len -= flen;
			fu_hdr[2] &= ~(1 << 7); /* clear Start bit */
		}

		fu_hdr[2] |= 1<<6;  /* set END bit */

		err |= pkth(marker, fu_hdr, 3, buf, len, arg);
	}

	return err;
}


int h265_encode(struct videnc_state *st, bool update,
		const struct vidframe *frame)
{
	x265_picture *pic_in = NULL, pic_out;
	x265_nal *nalv;
	uint32_t i, nalc = 0;
	int colorspace;
	int n, err = 0;

	if (!st || !frame)
		return EINVAL;

	switch (frame->fmt) {

	case VID_FMT_YUV420P:
		colorspace = X265_CSP_I420;
		break;

#if 0
	case VID_FMT_YUV444:
		colorspace = X265_CSP_I444;
		break;
#endif

	default:
		warning("h265: encode: pixel format not supported (%s)\n",
			vidfmt_name(frame->fmt));
		return EINVAL;
	}

	if (!st->x265 || !vidsz_cmp(&st->size, &frame->size)) {

		st->param->internalCsp = colorspace;

		err = open_encoder(st, &frame->size);
		if (err)
			return err;

		st->size = frame->size;
	}

	if (update) {
		debug("h265: encode: picture update was requested\n");
	}

	pic_in = x265_picture_alloc();
	if (!pic_in) {
		warning("h265: x265_picture_alloc failed\n");
		return ENOMEM;
	}

	x265_picture_init(st->param, pic_in);

	pic_in->sliceType  = update ? X265_TYPE_IDR : X265_TYPE_AUTO;
	pic_in->pts        = ++st->pts;      /* XXX: add PTS to API */
	pic_in->colorSpace = colorspace;

	for (i=0; i<3; i++) {
		pic_in->planes[i] = frame->data[i];
		pic_in->stride[i] = frame->linesize[i];
	}

	/* NOTE: important to get the PTS of the "out" picture */
	n = x265_encoder_encode(st->x265, &nalv, &nalc, pic_in, &pic_out);
	if (n <= 0)
		goto out;

	for (i=0; i<nalc; i++) {

		x265_nal *nal = &nalv[i];
		uint8_t *p = nal->payload;
		size_t len = nal->sizeBytes;
		bool marker;

#if 0
		debug("h265: encode: %s type=%2d  %s\n",
			  h265_is_keyframe(nal->type) ? "<KEY>" : "     ",
			  nal->type, h265_nalunit_name(nal->type));
#endif

		h265_skip_startcode(&p, &len);

		/* XXX: use pic_out.pts */

		marker = (i+1)==nalc;  /* last NAL */

		err = packetize(marker, p, len, st->pktsize,
				st->pkth, st->arg);
		if (err)
			goto out;
	}

 out:
	if (pic_in)
		x265_picture_free(pic_in);

	return err;
}
