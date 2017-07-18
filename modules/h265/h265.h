/**
 * @file h265.h H.265 Video Codec -- internal API
 *
 * Copyright (C) 2010 Creytiv.com
 */


/*
 * H.265 format
 */
enum {
	H265_HDR_SIZE = 2
};

enum h265_naltype {
	/* VCL class */
	H265_NAL_TRAIL_N         = 0,
	H265_NAL_TRAIL_R         = 1,

	H265_NAL_RASL_N          = 8,
	H265_NAL_RASL_R          = 9,

	H265_NAL_BLA_W_LP        = 16,
	H265_NAL_BLA_W_RADL      = 17,
	H265_NAL_BLA_N_LP        = 18,
	H265_NAL_IDR_W_RADL      = 19,
	H265_NAL_IDR_N_LP        = 20,
	H265_NAL_CRA_NUT         = 21,

	/* non-VCL class */
	H265_NAL_VPS_NUT         = 32,
	H265_NAL_SPS_NUT         = 33,
	H265_NAL_PPS_NUT         = 34,
	H265_NAL_PREFIX_SEI_NUT  = 39,
	H265_NAL_SUFFIX_SEI_NUT  = 40,

	/* draft-ietf-payload-rtp-h265 */
	H265_NAL_AP              = 48,    /* Aggregation Packets */
	H265_NAL_FU              = 49,
};

struct h265_nal {
	unsigned nal_unit_type:6;          /* NAL unit type (0-40)       */
	unsigned nuh_temporal_id_plus1:3;  /* temporal identifier plus 1 */
};

void h265_nal_encode(uint8_t buf[2], unsigned nal_unit_type,
		     unsigned nuh_temporal_id_plus1);
int  h265_nal_encode_mbuf(struct mbuf *mb, const struct h265_nal *nal);
int  h265_nal_decode(struct h265_nal *nal, const uint8_t *p);
void h265_nal_print(const struct h265_nal *nal);

bool h265_have_startcode(const uint8_t *p, size_t len);
void h265_skip_startcode(uint8_t **p, size_t *n);
bool h265_is_keyframe(enum h265_naltype type);
const char *h265_nalunit_name(enum h265_naltype type);


/* encoder */
int h265_encode_update(struct videnc_state **vesp, const struct vidcodec *vc,
		       struct videnc_param *prm, const char *fmtp,
		       videnc_packet_h *pkth, void *arg);
int h265_encode(struct videnc_state *ves, bool update,
		const struct vidframe *frame);

/* decoder */
int h265_decode_update(struct viddec_state **vdsp, const struct vidcodec *vc,
		       const char *fmtp);
int h265_decode(struct viddec_state *vds, struct vidframe *frame,
		bool *intra, bool marker, uint16_t seq, struct mbuf *mb);
