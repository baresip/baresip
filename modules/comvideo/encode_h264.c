
/**
 * @file encode_h264.c  Commend specific video source
 *
 * Copyright (C) 2020 Commend.com
 */

#include "comvideo.h"
#include <re_h264.h>

struct videnc_state {
	struct videnc_param encprm;
	videnc_packet_h *pkth;
	void *arg;
	unsigned pktsize;
	uint32_t packetization_mode;
};


typedef struct {
	guint8 *sample;
	gsize size;
} enc_data;


/* note: dummy function, the input is unused */
int encode_h264(struct videnc_state *st, bool update,
		const struct vidframe *frame, uint64_t timestamp)
{
	(void) st;
	(void) update;
	(void) frame;
	(void) timestamp;
	return 0;
}


static void enc_destructor(void *arg)
{
	struct videnc_state *st = arg;
	comvideo_codec.encoders = g_list_remove(comvideo_codec.encoders, st);
}


static void param_handler(const struct pl *name, const struct pl *val,
			  void *arg)
{
	struct videnc_state *st = arg;

	if (0 == pl_strcasecmp(name, "packetization-mode")) {
		st->packetization_mode = pl_u32(val);

		if (st->packetization_mode != 0 &&
		    st->packetization_mode != 1 ) {
			warning("comvideo: illegal packetization-mode %u\n",
				st->packetization_mode);
		}
	}
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

	if (str_isset(fmtp)) {
		struct pl sdp_fmtp;
		pl_set_str(&sdp_fmtp, fmtp);
		fmt_param_apply(&sdp_fmtp, param_handler, st);
	}

	info("comvideo: video encoder %s: %.2f fps, %d bit/s, pktsize=%u\n",
	      vc->name, prm->fps, prm->bitrate, prm->pktsize);
	*vesp = st;

	return 0;
}


static void
encode_h264_sample(struct videnc_state *st, enc_data *encData)
{
	guint8 *sample = encData->sample;
	gsize  size = encData->size;

	uint8_t *hdr = sample;
	uint8_t *pld = sample + RTP_HEADER_SIZE;
	size_t pld_len = size - RTP_HEADER_SIZE;

	guint32 ts = 0;
	bool marker;

	ts |= hdr[4] << 24;
	ts |= hdr[5] << 16;
	ts |= hdr[6] << 8;
	ts |= hdr[7];

	marker = hdr[1] >> 7;

	st->pkth(marker, ts, hdr, 0, pld, pld_len, st->arg);
}


void
camera_h264_sample_received(
	GstCameraSrc *src, GstSample *sample,
	struct vidsrc_st *st)
{
	GstBuffer *buffer;
	GstMapInfo map_info;
	enc_data encData;

	(void) src;
	(void) st;

	buffer = gst_sample_get_buffer(sample);
	gst_buffer_map(buffer, &map_info, (GstMapFlags) (GST_MAP_READ));

	if (map_info.size >= RTP_HEADER_SIZE && map_info.data) {
		encData.sample = map_info.data;
		encData.size = map_info.size;

		g_list_foreach(
			comvideo_codec.encoders,
			(GFunc) encode_h264_sample,
			&encData);
	}
	gst_buffer_unmap(buffer, &map_info);
}

