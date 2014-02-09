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
 * H.264
 */


/** NAL unit types (RFC 3984, Table 1) */
enum {
	H264_NAL_UNKNOWN      = 0,
	/* 1-23   NAL unit  Single NAL unit packet per H.264 */
	H264_NAL_SLICE        = 1,
	H264_NAL_DPA          = 2,
	H264_NAL_DPB          = 3,
	H264_NAL_DPC          = 4,
	H264_NAL_IDR_SLICE    = 5,
	H264_NAL_SEI          = 6,
	H264_NAL_SPS          = 7,
	H264_NAL_PPS          = 8,
	H264_NAL_AUD          = 9,
	H264_NAL_END_SEQUENCE = 10,
	H264_NAL_END_STREAM   = 11,
	H264_NAL_FILLER_DATA  = 12,
	H264_NAL_SPS_EXT      = 13,
	H264_NAL_AUX_SLICE    = 19,

	H264_NAL_STAP_A       = 24,  /**< Single-time aggregation packet */
	H264_NAL_STAP_B       = 25,  /**< Single-time aggregation packet */
	H264_NAL_MTAP16       = 26,  /**< Multi-time aggregation packet  */
	H264_NAL_MTAP24       = 27,  /**< Multi-time aggregation packet  */
	H264_NAL_FU_A         = 28,  /**< Fragmentation unit             */
	H264_NAL_FU_B         = 29,  /**< Fragmentation unit             */
};

/**
 * H.264 Header defined in RFC 3984
 *
 * <pre>
      +---------------+
      |0|1|2|3|4|5|6|7|
      +-+-+-+-+-+-+-+-+
      |F|NRI|  Type   |
      +---------------+
 * </pre>
 */
struct h264_hdr {
	unsigned f:1;      /**< 1 bit  - Forbidden zero bit (must be 0) */
	unsigned nri:2;    /**< 2 bits - nal_ref_idc                    */
	unsigned type:5;   /**< 5 bits - nal_unit_type                  */
};

int h264_hdr_encode(const struct h264_hdr *hdr, struct mbuf *mb);
int h264_hdr_decode(struct h264_hdr *hdr, struct mbuf *mb);

/** Fragmentation Unit header */
struct fu {
	unsigned s:1;      /**< Start bit                               */
	unsigned e:1;      /**< End bit                                 */
	unsigned r:1;      /**< The Reserved bit MUST be equal to 0     */
	unsigned type:5;   /**< The NAL unit payload type               */
};

int fu_hdr_encode(const struct fu *fu, struct mbuf *mb);
int fu_hdr_decode(struct fu *fu, struct mbuf *mb);

const uint8_t *h264_find_startcode(const uint8_t *p, const uint8_t *end);

int h264_decode_sprop_params(AVCodecContext *codec, struct pl *pl);
