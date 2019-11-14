/**
 * @file mock/mock_vidcodec.c Mock video codec
 *
 * Copyright (C) 2010 - 2016 Creytiv.com
 */

#include <string.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "../test.h"


#define HDR_SIZE 12


struct hdr {
	enum vidfmt fmt;
	unsigned width;
	unsigned height;
};

struct videnc_state {
	double fps;
	videnc_packet_h *pkth;
	void *arg;
};

struct viddec_state {
	struct vidframe *frame;
};


static int hdr_decode(struct hdr *hdr, struct mbuf *mb)
{
	if (mbuf_get_left(mb) < HDR_SIZE)
		return EBADMSG;

	hdr->fmt    = ntohl(mbuf_read_u32(mb));
	hdr->width  = ntohl(mbuf_read_u32(mb));
	hdr->height = ntohl(mbuf_read_u32(mb));

	return 0;
}


static void decode_destructor(void *arg)
{
	struct viddec_state *vds = arg;

	mem_deref(vds->frame);
}


static int mock_encode_update(struct videnc_state **vesp,
			      const struct vidcodec *vc,
			      struct videnc_param *prm, const char *fmtp,
			      videnc_packet_h *pkth, void *arg)
{
	struct videnc_state *ves;
	(void)fmtp;

	if (!vesp || !vc || !prm || prm->pktsize < (HDR_SIZE + 1))
		return EINVAL;

	ves = *vesp;

	if (!ves) {

		ves = mem_zalloc(sizeof(*ves), NULL);
		if (!ves)
			return ENOMEM;

		*vesp = ves;
	}

	ves->fps     = prm->fps;
	ves->pkth    = pkth;
	ves->arg     = arg;

	return 0;
}


static int mock_encode(struct videnc_state *ves, bool update,
		       const struct vidframe *frame, uint64_t timestamp)
{
	struct mbuf *hdr;
	uint8_t payload[2] = {0,0};
	uint64_t rtp_ts;
	int err;
	(void)update;

	if (!ves || !frame)
		return EINVAL;

	hdr = mbuf_alloc(16);

	err  = mbuf_write_u32(hdr, htonl(frame->fmt));
	err |= mbuf_write_u32(hdr, htonl(frame->size.w));
	err |= mbuf_write_u32(hdr, htonl(frame->size.h));
	if (err)
		goto out;

	rtp_ts = video_calc_rtp_timestamp_fix(timestamp);

	err = ves->pkth(true, rtp_ts, hdr->buf, hdr->end,
			payload, sizeof(payload), ves->arg);
	if (err)
		goto out;

 out:
	mem_deref(hdr);

	return err;
}


static int mock_decode_update(struct viddec_state **vdsp,
			      const struct vidcodec *vc, const char *fmtp)
{
	struct viddec_state *vds;
	(void)vc;
	(void)fmtp;

	if (!vdsp)
		return EINVAL;

	vds = *vdsp;

	if (vds)
		return 0;

	vds = mem_zalloc(sizeof(*vds), decode_destructor);
	if (!vds)
		return ENOMEM;

	*vdsp = vds;

	return 0;
}


static int mock_decode(struct viddec_state *vds, struct vidframe *frame,
		       bool *intra, bool marker, uint16_t seq, struct mbuf *mb)
{
	struct vidsz size;
	struct hdr hdr;
	int err, i;
	(void)marker;
	(void)seq;

	if (!vds || !frame || !intra || !mb)
		return EINVAL;

	*intra = false;

	err = hdr_decode(&hdr, mb);
	if (err) {
		warning("mock_vidcodec: could not decode header (%m)\n", err);
		return err;
	}

	size.w = hdr.width;
	size.h = hdr.height;

	if (!vds->frame) {
		err = vidframe_alloc(&vds->frame, hdr.fmt, &size);
		if (err)
			goto out;
	}

	for (i=0; i<4; i++) {
		frame->data[i]     = vds->frame->data[i];
		frame->linesize[i] = vds->frame->linesize[i];
	}

	frame->size.w = vds->frame->size.w;
	frame->size.h = vds->frame->size.h;
	frame->fmt    = vds->frame->fmt;

 out:
	return err;
}


static struct vidcodec vc_dummy = {
	.name      = "H266",
	.encupdh   = mock_encode_update,
	.ench      = mock_encode,
	.decupdh   = mock_decode_update,
	.dech      = mock_decode,
};


void mock_vidcodec_register(void)
{
	vidcodec_register(baresip_vidcodecl(), &vc_dummy);
}


void mock_vidcodec_unregister(void)
{
	vidcodec_unregister(&vc_dummy);
}
