/**
 * @file celt.c  CELT (Code-Excited Lapped Transform) audio codec
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <stdlib.h>
#include <celt/celt.h>
#include <re.h>
#include <baresip.h>


#define DEBUG_MODULE "celt"
#define DEBUG_LEVEL 5
#include <re_dbg.h>


/**
 * @defgroup celt celt
 *
 * CELT audio codec
 *
 * @deprecated Replaced by the @ref opus module
 *
 * NOTE:
 *
 * The CELT codec has been merged into the IETF Opus codec and is now obsolete
 */


#ifdef CELT_GET_FRAME_SIZE
#define CELT_OLD_API 1
#endif


/** Celt constants */
enum {
	DEFAULT_FRAME_SIZE = 640,   /**< Framesize in [samples]    */
	DEFAULT_BITRATE    = 64000, /**< 32-128 kbps               */
	DEFAULT_PTIME      = 20,    /**< Packet time in [ms]       */
	MAX_FRAMES         = 16     /**< Maximum frames per packet */
};


struct aucodec_st {
	struct aucodec *ac;         /**< Inheritance - base class     */
	CELTMode *mode;             /**< Shared CELT mode             */
	CELTEncoder *enc;           /**< CELT Encoder state           */
	CELTDecoder *dec;           /**< CELT Decoder state           */
	int32_t frame_size;         /**< Frame size in [samples]      */
	uint32_t bitrate;           /**< Bit-rate in [bit/s]          */
	uint32_t fsize;             /**< PCM Frame size in bytes      */
	uint32_t bytes_per_packet;  /**< Encoded packet size in bytes */
	bool low_overhead;          /**< Low-Overhead Mode            */
	uint16_t bpfv[MAX_FRAMES];  /**< Bytes per Frame vector       */
	uint16_t bpfn;              /**< Number of 'Bytes per Frame'  */
};


/* Configurable items: */
static uint32_t celt_low_overhead = 0;  /* can be 0 or 1 */
static struct aucodec *celtv[2];


static void celt_destructor(void *arg)
{
	struct aucodec_st *st = arg;

	if (st->enc)
		celt_encoder_destroy(st->enc);
	if (st->dec)
		celt_decoder_destroy(st->dec);

	if (st->mode)
		celt_mode_destroy(st->mode);

	mem_deref(st->ac);
}


static void decode_param(const struct pl *name, const struct pl *val,
			 void *arg)
{
	struct aucodec_st *st = arg;
	int err;

	if (0 == pl_strcasecmp(name, "bitrate")) {
		st->bitrate = pl_u32(val) * 1000;
	}
	else if (0 == pl_strcasecmp(name, "frame-size")) {
		st->frame_size = pl_u32(val);

		if (st->frame_size & 0x1) {
			DEBUG_WARNING("frame-size is NOT even: %u\n",
				      st->frame_size);
		}
	}
	else if (0 == pl_strcasecmp(name, "low-overhead")) {
		struct pl fs, bpfv;
		uint32_t i;

		st->low_overhead = true;

		err = re_regex(val->p, val->l, "[0-9]+/[0-9,]+", &fs, &bpfv);
		if (err)
			return;

		st->frame_size = pl_u32(&fs);

		for (i=0; i<ARRAY_SIZE(st->bpfv) && bpfv.l > 0; i++) {
			struct pl bpf, co;

			co.l = 0;
			if (re_regex(bpfv.p, bpfv.l, "[0-9]+[,]*", &bpf, &co))
				break;

			pl_advance(&bpfv, bpf.l + co.l);

			st->bpfv[i] = pl_u32(&bpf);
		}
		st->bpfn = i;
	}
	else {
		DEBUG_NOTICE("unknown param: %r = %r\n", name, val);
	}
}


static int decode_params(struct aucodec_st *st, const char *fmtp)
{
	struct pl params;

	pl_set_str(&params, fmtp);

	fmt_param_apply(&params, decode_param, st);

	return 0;
}


static int alloc(struct aucodec_st **stp, struct aucodec *ac,
		 struct aucodec_prm *encp, struct aucodec_prm *decp,
		 const char *fmtp)
{
	struct aucodec_st *st;
	const uint32_t srate = aucodec_srate(ac);
	const uint8_t ch = aucodec_ch(ac);
	int err = 0;

	(void)decp;

	st = mem_zalloc(sizeof(*st), celt_destructor);
	if (!st)
		return ENOMEM;

	st->ac = mem_ref(ac);

	st->bitrate      = DEFAULT_BITRATE;
	st->low_overhead = celt_low_overhead;

	if (encp && encp->ptime) {
		st->frame_size = srate * ch * encp->ptime / 1000;
		DEBUG_NOTICE("calc ptime=%u  ---> frame_size=%u\n",
			     encp->ptime, st->frame_size);
	}
	else {
		st->frame_size = DEFAULT_FRAME_SIZE;
	}

	if (str_isset(fmtp))
		decode_params(st, fmtp);

	/* Common mode */
	st->mode = celt_mode_create(srate, st->frame_size, NULL);
	if (!st->mode) {
		DEBUG_WARNING("alloc: could not create CELT mode\n");
		err = EPROTO;
		goto out;
	}

#ifdef CELT_GET_FRAME_SIZE
	celt_mode_info(st->mode, CELT_GET_FRAME_SIZE, &st->frame_size);
#endif

	st->fsize = 2 * st->frame_size * ch;
	st->bytes_per_packet = (st->bitrate * st->frame_size / srate + 4)/8;

	DEBUG_NOTICE("alloc: frame_size=%u bitrate=%ubit/s fsize=%u"
		     " bytes_per_packet=%u\n",
		     st->frame_size, st->bitrate, st->fsize,
		     st->bytes_per_packet);

	/* Encoder */
#ifdef CELT_OLD_API
	st->enc = celt_encoder_create(st->mode, ch, NULL);
#else
	st->enc = celt_encoder_create(srate, ch, NULL);
#endif
	if (!st->enc) {
		DEBUG_WARNING("alloc: could not create CELT encoder\n");
		err = EPROTO;
		goto out;
	}

	/* Decoder */
#ifdef CELT_OLD_API
	st->dec = celt_decoder_create(st->mode, ch, NULL);
#else
	st->dec = celt_decoder_create(srate, ch, NULL);
#endif
	if (!st->dec) {
		DEBUG_WARNING("alloc: could not create CELT decoder\n");
		err = EPROTO;
		goto out;
	}

 out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}


static int encode_frame(struct aucodec_st *st, uint16_t *size, uint8_t *buf,
			struct mbuf *src)
{
	int len;

	/* NOTE: PCM audio in signed 16-bit format (native endian) */
	len = celt_encode(st->enc, (short *)mbuf_buf(src), st->frame_size,
			  buf, st->bytes_per_packet);
	if (len < 0) {
		DEBUG_WARNING("celt_encode: returned %d\n", len);
		return EINVAL;
	}

	DEBUG_INFO("encode: %u -> %d\n", mbuf_get_left(src), len);

	*size = len;

	mbuf_advance(src, st->fsize);

	return 0;
}


static int encode(struct aucodec_st *st, struct mbuf *dst, struct mbuf *src)
{
	struct {
		uint8_t buf[1024];
		uint16_t len;
	} framev[MAX_FRAMES];
	uint32_t i;
	size_t n;
	int err = 0;

	n = src->end / st->fsize;
	if (n > MAX_FRAMES) {
		n = MAX_FRAMES;
		DEBUG_WARNING("number of frames truncated to %u\n", n);
	}

	DEBUG_INFO("enc: %u bytes into %u frames\n", src->end, n);

	if (n ==0) {
		DEBUG_WARNING("enc: short frame (%u < %u)\n",
			      src->end, st->fsize);
		return EINVAL;
	}

	/* Encode all frames into temp buffer */
	for (i=0; i<n && !err; i++) {
		framev[i].len = sizeof(framev[i].buf);
		err = encode_frame(st, &framev[i].len, framev[i].buf, src);
	}

	if (!st->low_overhead) {
		/* Encode all length headers */
		for (i=0; i<n && !err; i++) {
			uint16_t len = framev[i].len;

			while (len >= 0xff) {
				err = mbuf_write_u8(dst, 0xff);
				len -= 0xff;
			}
			err = mbuf_write_u8(dst, len);
		}
	}

	/* Encode all frame buffers */
	for (i=0; i<n && !err; i++) {
		err = mbuf_write_mem(dst, framev[i].buf, framev[i].len);
	}

	return err;
}


static int decode_frame(struct aucodec_st *st, struct mbuf *dst,
			struct mbuf *src, uint16_t src_len)
{
	int ret, err;

	if (mbuf_get_left(src) < src_len) {
		DEBUG_WARNING("dec: corrupt frame %u < %u\n",
			      mbuf_get_left(src), src_len);
		return EPROTO;
	}

	/* Make sure there is enough space in the buffer */
	if (mbuf_get_space(dst) < st->fsize) {
		err = mbuf_resize(dst, dst->size + st->fsize);
		if (err)
			return err;
	}

	ret = celt_decode(st->dec, mbuf_buf(src), src_len,
			  (short *)mbuf_buf(dst), st->frame_size);
	if (CELT_OK != ret) {
		DEBUG_WARNING("celt_decode: ret=%d\n", ret);
	}

	DEBUG_INFO("decode: %u -> %u\n", src_len, st->fsize);

	if (src)
		mbuf_advance(src, src_len);

	dst->end += st->fsize;

	return 0;
}


/* src=NULL means lost packet */
static int decode(struct aucodec_st *st, struct mbuf *dst, struct mbuf *src)
{
	uint16_t lengthv[MAX_FRAMES];
	uint16_t total_length = 0;
	uint32_t i, n;
	int err = 0;

	DEBUG_INFO("decode %u bytes\n", mbuf_get_left(src));

	if (st->low_overhead) {
		/* No length bytes */
		for (i=0; i<st->bpfn && !err; i++) {
			err = decode_frame(st, dst, src, st->bpfv[i]);
		}
	}
	else {
		bool done = false;

		/* Read the length bytes */
		for (i=0; i<ARRAY_SIZE(lengthv) && !done; i++) {
			uint8_t byte;

			if (mbuf_get_left(src) < 1)
				return EPROTO;

			/* Decode length */
			lengthv[i] = 0;
			do {
				byte = mbuf_read_u8(src);
				lengthv[i] += byte;
			}
			while (byte == 0xff);

			total_length += lengthv[i];

			if (total_length >= mbuf_get_left(src))
				done = true;
		}
		n = i;
		DEBUG_INFO("decoded %d frames\n", n);

		for (i=0; i<n && !err; i++) {
			err = decode_frame(st, dst, src, lengthv[i]);
		}
	}

	return err;
}


static int module_init(void)
{
	int err;

	err  = aucodec_register(&celtv[0], 0, "CELT", 48000, 1, NULL,
				alloc, encode, decode, NULL);
	err |= aucodec_register(&celtv[1], 0, "CELT", 32000, 1, NULL,
				alloc, encode, decode, NULL);

	return err;
}


static int module_close(void)
{
	size_t i;

	for (i=0; i<ARRAY_SIZE(celtv); i++)
		celtv[i] = mem_deref(celtv[i]);

	return 0;
}


/** Module exports */
EXPORT_SYM const struct mod_export DECL_EXPORTS(celt) = {
	"celt",
	"codec",
	module_init,
	module_close
};
