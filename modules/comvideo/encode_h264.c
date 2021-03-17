/**
 * @file encode_h264.c  Commend specific video source
 *
 * Copyright (C) 2020 Commend.com
 */

#include "comvideo.h"

struct videnc_state {
	struct videnc_param encprm;
	videnc_packet_h *pkth;
	void *arg;
	unsigned pktsize;
};


typedef struct {
	guint8 *sample;
	gsize size;
	GstClockTime ts;
} enc_data;


/* note: dummy function, the input is unused */
int encode_h264(struct videnc_state *st, bool update,
		const struct vidframe *frame, uint64_t timestamp) {
	(void) st;
	(void) update;
	(void) frame;
	(void) timestamp;

	return 0;
}


static void enc_destructor(void *arg) {
	struct videnc_state *st = arg;

	comvideo_codec.encoders = g_list_remove(comvideo_codec.encoders, st);
}


int encode_h264_update(struct videnc_state **vesp, const struct vidcodec *vc,
		       struct videnc_param *prm, const char *fmtp,
		       videnc_packet_h *pkth, void *arg) {
	struct videnc_state *st;

	(void) fmtp;

	if (!vesp || !vc || !prm || !pkth)
		return EINVAL;

	if (*vesp)
		return 0;

	st = mem_zalloc(sizeof(*st), enc_destructor);
	if (!st)
		return ENOMEM;

	st->encprm = *prm;
	st->pkth = pkth;
	st->arg = arg;
	st->pktsize = prm->pktsize;

	comvideo_codec.encoders = g_list_append(comvideo_codec.encoders, st);

	info("comvideo: video encoder %s: %.2f fps, %d bit/s, pktsize=%u\n",
	     vc->name, prm->fps, prm->bitrate, prm->pktsize);
	*vesp = st;

	return 0;
}


static void
encode_h264_sample(struct videnc_state *st, enc_data *encData) {
	h264_packetize(
		encData->ts,
		encData->sample,
		encData->size,
		st->pktsize,
		st->pkth,
		st->arg);
}


void
camera_h264_sample_received(
	GstCameraSrc *src, GstSample *sample,
	struct vidsrc_st *st)
{
	GstBuffer *buffer;
	GstMapInfo info;
	enc_data encData;

	(void) src;
	(void) st;

	buffer = gst_sample_get_buffer(sample);
	gst_buffer_map(buffer, &info, (GstMapFlags)(GST_MAP_READ));

	encData.sample = info.data;
	encData.size = info.size;
	encData.ts = GST_BUFFER_DTS_OR_PTS(buffer);

	g_list_foreach(
		comvideo_codec.encoders,
		(GFunc) encode_h264_sample,
		&encData);

	gst_buffer_unmap(buffer, &info);
}

