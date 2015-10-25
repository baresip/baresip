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

int h264_decode_sprop_params(AVCodecContext *codec, struct pl *pl);
