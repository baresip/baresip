/**
 * @file h26x.h  Interface to H.26x video codecs
 *
 * Copyright (C) 2010 Creytiv.com
 */


/*
 * H.263
 */


enum h263_mode {
	H263_MODE_A,
	H263_MODE_B,
	H263_MODE_C
};

enum {
	H263_HDR_SIZE_MODEA = 4,
	H263_HDR_SIZE_MODEB = 8,
	H263_HDR_SIZE_MODEC = 12
};

/** H.263 picture size format */
enum h263_fmt {
	H263_FMT_SQCIF = 1, /**<  128 x 96   */
	H263_FMT_QCIF  = 2, /**<  176 x 144  */
	H263_FMT_CIF   = 3, /**<  352 x 288  */
	H263_FMT_4CIF  = 4, /**<  704 x 576  */
	H263_FMT_16CIF = 5, /**< 1408 x 1152 */
	H263_FMT_OTHER = 7,
};

/**
 * H.263 Header defined in RFC 2190
 */
struct h263_hdr {

	/* common */
	unsigned f:1;      /**< 1 bit  - Flag; 0=mode A, 1=mode B/C         */
	unsigned p:1;      /**< 1 bit  - PB-frames, 0=mode B, 1=mode C      */
	unsigned sbit:3;   /**< 3 bits - Start Bit Position (SBIT)          */
	unsigned ebit:3;   /**< 3 bits - End Bit Position (EBIT)            */
	unsigned src:3;    /**< 3 bits - Source format                      */

	/* mode A */
	unsigned i:1;      /**< 1 bit  - 0=intra-coded, 1=inter-coded       */
	unsigned u:1;      /**< 1 bit  - Unrestricted Motion Vector         */
	unsigned s:1;      /**< 1 bit  - Syntax-based Arithmetic Coding     */
	unsigned a:1;      /**< 1 bit  - Advanced Prediction option         */
	unsigned r:4;      /**< 4 bits - Reserved (zero)                    */
	unsigned dbq:2;    /**< 2 bits - DBQUANT                            */
	unsigned trb:3;    /**< 3 bits - Temporal Reference for B-frame     */
	unsigned tr:8;     /**< 8 bits - Temporal Reference for P-frame     */

	/* mode B */
	unsigned quant:5; //=0 for GOB header
	unsigned gobn:5;  // gob number
	unsigned mba:9;   // address
	unsigned hmv1:7;  // horizontal motion vector
	unsigned vmv1:7;  // vertical motion vector
	unsigned hmv2:7;
	unsigned vmv2:7;


};

enum {I_FRAME=0, P_FRAME=1};

/** H.263 bit-stream header */
struct h263_strm {
	uint8_t psc[2];              /**< Picture Start Code (PSC)        */

	uint8_t temp_ref;            /**< Temporal Reference              */
	unsigned split_scr:1;        /**< Split Screen Indicator          */
	unsigned doc_camera:1;       /**< Document Camera Indicator       */
	unsigned pic_frz_rel:1;      /**< Full Picture Freeze Release     */
	unsigned src_fmt:3;          /**< Source Format. 3=CIF            */
	unsigned pic_type:1;         /**< Picture Coding Type. 0=I, 1=P   */
	unsigned umv:1;              /**< Unrestricted Motion Vector mode */
	unsigned sac:1;              /**< Syntax-based Arithmetic Coding  */
	unsigned apm:1;              /**< Advanced Prediction mode        */
	unsigned pb:1;               /**< PB-frames mode                  */
	unsigned pquant:5;           /**< Quantizer Information           */
	unsigned cpm:1;              /**< Continuous Presence Multipoint  */
	unsigned pei:1;              /**< Extra Insertion Information     */
	/* H.263 bit-stream ... */
};

int h263_hdr_encode(const struct h263_hdr *hdr, struct mbuf *mb);
int h263_hdr_decode(struct h263_hdr *hdr, struct mbuf *mb);
enum h263_mode h263_hdr_mode(const struct h263_hdr *hdr);

const uint8_t *h263_strm_find_psc(const uint8_t *p, uint32_t size);
int  h263_strm_decode(struct h263_strm *s, struct mbuf *mb);
void h263_hdr_copy_strm(struct h263_hdr *hdr, const struct h263_strm *s);


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

	H265_NAL_TSA_N           = 2,
	H265_NAL_TSA_R           = 3,

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

const uint8_t *h265_find_startcode(const uint8_t *p, const uint8_t *end);
bool h265_have_startcode(const uint8_t *p, size_t len);
void h265_skip_startcode(uint8_t **p, size_t *n);
bool h265_is_keyframe(enum h265_naltype type);
const char *h265_nalunit_name(enum h265_naltype type);
int h265_packetize(uint64_t rtp_ts, const uint8_t *buf, size_t len,
		   size_t pktsize, videnc_packet_h *pkth, void *arg);
