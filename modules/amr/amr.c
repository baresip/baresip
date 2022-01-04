/**
 * @file amr.c Adaptive Multi-Rate (AMR) audio codec
 *
 * Copyright (C) 2010 Alfred E. Heggestad
 */
#include <stdlib.h>
#include <string.h>
#ifdef AMR_NB
#include <interf_enc.h>
#include <interf_dec.h>
#endif
#ifdef AMR_WB
#ifdef _TYPEDEF_H
#define typedef_h
#endif
#include <enc_if.h>
#include <dec_if.h>
#endif
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "amr.h"


#ifdef VO_AMRWBENC_ENC_IF_H
#define IF2E_IF_encode E_IF_encode
#define IF2D_IF_decode D_IF_decode
#endif


/**
 * @defgroup amr amr
 *
 * This module supports both AMR Narrowband (8000 Hz) and
 * AMR Wideband (16000 Hz) audio codecs.
 *
 * Reference:
 *
 *     http://tools.ietf.org/html/rfc4867
 *
 *     http://www.penguin.cz/~utx/amr
 */


#ifndef SERIAL_MAX
/* Maximum size of buffer passed to encoder/decoder */
#define SERIAL_MAX 61
#endif

/* Narrowband encoder bitrate = 12.2kbps */
#define NB_MODE MR122
/* Wideband encoder bitrate = 23.85 kbps */
#define WB_MODE 8

/* Bits per speech frame for a given bitrate/frame type.
  -1 is an invalid frame type. */
static const int frame_size_nb[16] = {
	95, 103, 118, 134, 148, 159, 204, 244,
	40,  -1,  -1,  -1,  -1,  -1,  -1,   0
};

static const int frame_size_wb[16] = {
	132, 177, 253, 285, 317, 365, 397, 461,
	477,  40,  -1,  -1,  -1,  -1,  -1,   0
};

/* Samples per 20ms speech frame */
enum {
	NB_SAMPS_PER_FRAME = 160,
	WB_SAMPS_PER_FRAME = 320
};


struct auenc_state {
	const struct amr_aucodec *ac;
	void *enc;                  /**< Encoder state            */
};

struct audec_state {
	const struct amr_aucodec *ac;
	void *dec;                  /**< Decoder state            */
};


static void encode_destructor(void *arg)
{
	struct auenc_state *st = arg;

	switch (st->ac->ac.srate) {

#ifdef AMR_NB
	case 8000:
		Encoder_Interface_exit(st->enc);
		break;
#endif

#ifdef AMR_WB
	case 16000:
		E_IF_exit(st->enc);
		break;
#endif
	}
}


static void decode_destructor(void *arg)
{
	struct audec_state *st = arg;
	struct amr_aucodec *amr_ac = (struct amr_aucodec *)st->ac;

	switch (st->ac->ac.srate) {

#ifdef AMR_NB
	case 8000:
		Decoder_Interface_exit(st->dec);

		mem_deref(amr_ac->dec_arr);
		mem_deref(amr_ac->enc_arr);

		break;
#endif

#ifdef AMR_WB
	case 16000:
		D_IF_exit(st->dec);

		mem_deref(amr_ac->dec_arr);
		mem_deref(amr_ac->enc_arr);

		break;
#endif
	}
}


static int encode_update(struct auenc_state **aesp,
			 const struct aucodec *ac,
			 struct auenc_param *prm, const char *fmtp)
{
	struct amr_aucodec *amr_ac = (struct amr_aucodec *)ac;
	struct auenc_state *st;
	int err = 0;
	(void)prm;

	if (!aesp || !ac)
		return EINVAL;

	if (*aesp)
		return 0;

	st = mem_zalloc(sizeof(*st), encode_destructor);
	if (!st)
		return ENOMEM;

	st->ac = amr_ac;
	amr_ac->aligned = amr_octet_align(fmtp);

	switch (ac->srate) {

#ifdef AMR_NB
	case 8000:
		st->enc = Encoder_Interface_init(0);
		break;
#endif

#ifdef AMR_WB
	case 16000:
		st->enc = E_IF_init();
		break;
#endif
	}

	if (!st->enc)
		err = ENOMEM;

	if (err)
		mem_deref(st);
	else
		*aesp = st;

	return err;
}


static int decode_update(struct audec_state **adsp,
			 const struct aucodec *ac, const char *fmtp)
{
	struct amr_aucodec *amr_ac = (struct amr_aucodec *)ac;
	struct audec_state *st;
	int err = 0;

	if (!adsp || !ac)
		return EINVAL;

	if (*adsp)
		return 0;

	st = mem_zalloc(sizeof(*st), decode_destructor);
	if (!st)
		return ENOMEM;

	st->ac = amr_ac;
	amr_ac->aligned = amr_octet_align(fmtp);

	switch (ac->srate) {

#ifdef AMR_NB
	case 8000:
		st->dec = Decoder_Interface_init();

		amr_ac->dec_arr = mem_zalloc(SERIAL_MAX, NULL);
		amr_ac->enc_arr = mem_zalloc(SERIAL_MAX, NULL);
		if (!amr_ac->dec_arr || !amr_ac->enc_arr)
			err = ENOMEM;
		break;
#endif

#ifdef AMR_WB
	case 16000:
		st->dec = D_IF_init();

		amr_ac->dec_arr = mem_zalloc(1+SERIAL_MAX, NULL);
		amr_ac->enc_arr = mem_zalloc(1+SERIAL_MAX, NULL);

		if (!amr_ac->dec_arr || !amr_ac->enc_arr)
			err = ENOMEM;
		break;
#endif
	}

	if (!st->dec)
		err = ENOMEM;

	if (err)
		mem_deref(st);
	else
		*adsp = st;

	return err;
}

static void decode_wrapper(struct audec_state *st, int16_t *speech) {
	if (st->ac->ac.srate == 8000) {
#ifdef AMR_NB
		Decoder_Interface_Decode(st->dec, st->ac->dec_arr, speech, 0);
#endif
	} else if (st->ac->ac.srate == 16000) {
#ifdef AMR_WB
		IF2D_IF_decode(st->dec, st->ac->dec_arr, speech, 0);
#endif
	}
}

static int decode_be(struct audec_state *st, const uint8_t *buf, size_t const len, uint32_t const samps_per_frame, const int * frame_size_tbl, void *sampv, size_t *sampc)
{
	const struct amr_aucodec *amr_ac;
	signed int i;
	int num_frames = 0;
	uint32_t toc_bit_pos, speech_bit_pos, eof_bit_pos;
	uint8_t temp, FT;
	bool more_toc = 1;
	uint8_t *p_in;
	int16_t *p_out;

	amr_ac = (struct amr_aucodec *)st->ac;
	p_out = sampv;

	/*
	 * Example AMR header for a payload that contains 6 AMR speech frames:
	 *  0                   1                   2                   3
	 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9
	 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 * | CMR=1 |F|   FT  |Q|F|   FT  |Q|F|   FT  |Q|F|   FT  |Q|F|   FT  |Q|F|   FT  |Q|
	 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	*/
	/* Start unpacking after the CMR field */
	toc_bit_pos = 4;
	/* Loop through TOC until we find an F=0 entry while counting number of frames
	 * and bytes required to hold the unpacked payload */
	while (more_toc) {
		/*
		 * Unpack TOC entry into left aligned temp variable:
		 *         0 1 2 3 4 5 6 7
		 *         +-+-+-+-+-+-+-+
		 * temp = |F|   FT  |Q|X|X|
		 *         +-+-+-+-+-+-+-+
		*/
		temp = (buf[toc_bit_pos/8] << (toc_bit_pos % 8)) | (buf[(toc_bit_pos+6)/8] >> (8 - (toc_bit_pos % 8)));
		more_toc = !!(temp & 0x80);
		FT = (temp & 0x78) >> 3;
		if (FT >= 16 || frame_size_tbl[FT] < 0 || toc_bit_pos / 8 > len-1)
			return EPROTO;
		num_frames++;
		/* Increment bit_pos past this TOC entry */
		toc_bit_pos += 6;
	}

	/* Reset the toc "pointer" back to the beginning and keep track of the beginning of the speech bits */
	speech_bit_pos = toc_bit_pos;
	toc_bit_pos = 4;

	/* Iterate through all the speech frames, converting bandwidth-effecient data into octet-aligned data
	 * stored in dec_arr before passing it to the decoder */
	for (i = 0; i < num_frames; i++) {
		/* Convert TOC to octet-aligned version and place at beginning of buffer */
		temp = (buf[toc_bit_pos/8] << (toc_bit_pos % 8)) | (buf[(toc_bit_pos+6)/8] >> (8 - (toc_bit_pos % 8)));
		FT = (temp & 0x78) >> 3;
		toc_bit_pos += 6;
		p_in = amr_ac->dec_arr;
		*p_in++ = temp & 0x7C;

		/* Find the end of this speech frame and iterate through it unpacking 8 bits at a time */
		eof_bit_pos = speech_bit_pos + frame_size_tbl[FT];
		if (eof_bit_pos / 8 > len)
			return EPROTO;
		while (speech_bit_pos < eof_bit_pos) {
			/* Unpack all of the LSBs in the payload byte up to the current speech bit "pointer".
			 * Any bits past the end of the frame will be masked later. */
			uint8_t shift = speech_bit_pos % 8;
			*p_in = buf[speech_bit_pos/8] << shift;

			if (speech_bit_pos + 8 < eof_bit_pos) {
				/* If there were more than 8 bits remaining, unpack the MSBs of the next payload byte */
				speech_bit_pos += (8-shift);
				if (shift) {
					/* If shift==0, then we're already on a byte boundary and don't need to shift in the
					 * rest of the bits from the next payload byte */
					*p_in |= buf[speech_bit_pos/8] >> (8 - shift);
					/* Don't need to check for overflow here because we already made sure we needed
					 * more than 8 bits */
					speech_bit_pos += shift;
				}
			} else {
				uint8_t mask;
				/* Unpack bits from the next payload byte if we're not at the end of the frame yet */
				if (speech_bit_pos + (8-shift) < eof_bit_pos) {
					speech_bit_pos += (8-shift);
					*p_in |= buf[speech_bit_pos/8] >> (8 - shift);
				}
				/* At this point it's possible the unpacked buffer has some bits from the next speech frame.  Mask them off. */
				mask = ~((1<<(8-(eof_bit_pos - speech_bit_pos))) - 1);
				*p_in &= mask;
				speech_bit_pos = eof_bit_pos;
			}
			p_in++;
		}
		decode_wrapper(st, p_out);
		*sampc += samps_per_frame;
		p_out += samps_per_frame;
	}

	return 0;
}

static int encode_wrapper(struct auenc_state *st, const int16_t *speech) {
	int n = 0;
	if (st->ac->ac.srate == 8000) {
#ifdef AMR_NB
		n = Encoder_Interface_Encode(st->enc, NB_MODE, speech, st->ac->enc_arr, 0);
#endif
	} else if (st->ac->ac.srate == 16000) {
#ifdef AMR_WB
		n = IF2E_IF_encode(st->enc, WB_MODE, speech, st->ac->enc_arr, 0);
#endif
	}
	return n;
}


static int encode_be(struct auenc_state *st, const int16_t *p_in, uint32_t const frame_cnt, uint32_t const samps_per_frame, const int * frame_size_tbl, uint8_t *buf, size_t *len) {
	const uint8_t CMR_BITS = 4;
	const uint8_t F_BITS = 1;
	const uint8_t FT_BITS = 4;
	const uint8_t Q_BITS = 1;

	const struct amr_aucodec *amr_ac = (struct amr_aucodec *)st->ac;
	uint32_t frame_num = 0;
	uint32_t toc_bit_pos, speech_bit_pos, eof_bit_pos = 0;
	uint8_t bits_remaining;
	int encoded_bytes;
	int i;

	/* Clear buffer to make bitmasking a little less complicated */
	memset(buf, 0, *len);
	/* Set CMR field */
	buf[0] = 15<<CMR_BITS;
	toc_bit_pos = CMR_BITS;

	speech_bit_pos = CMR_BITS + frame_cnt * (F_BITS + FT_BITS + Q_BITS);
	while (frame_num < frame_cnt) {
		uint8_t FT;

		encoded_bytes = encode_wrapper(st, p_in);
		if (encoded_bytes < 0)
			return EPROTO;

		/* Set F field for TOC entry */
		if (frame_num != frame_cnt-1)
			buf[toc_bit_pos/8] |= 1 << (8 - ((toc_bit_pos % 8) + F_BITS));
		toc_bit_pos++;

		/* Extract FT and use it to determine the bit position of the next frame */
		FT = (amr_ac->enc_arr[0] >> 3) & 0x0F;
		if (FT >= 16 || frame_size_tbl[FT] < 0)
			return EPROTO;
		eof_bit_pos = speech_bit_pos + frame_size_tbl[FT];

		/* Determine if FT field will span an octet boundary */
		bits_remaining = 8 - (toc_bit_pos % 8);
		if (bits_remaining >= FT_BITS) {
			buf[toc_bit_pos/8] |= FT << (bits_remaining - FT_BITS);
		} else {
			uint32_t idx = toc_bit_pos/8;
			buf[idx] |= FT >> (FT_BITS - bits_remaining);
			/*           |<---    Mask FT bits in prev byte   --->|  |<---  Shift into MSb  --->| */
			buf[idx+1] |= (FT & ((1 << (FT_BITS-bits_remaining))-1))<<(8-(FT_BITS-bits_remaining));
		}
		toc_bit_pos += FT_BITS;
		buf[toc_bit_pos/8] |= (amr_ac->enc_arr[0]>>2 & 0x01) << (8 - ((toc_bit_pos % 8) + Q_BITS));
		toc_bit_pos++;

		for (i=1; i<encoded_bytes; i++) {
			uint32_t idx = speech_bit_pos/8;
			bits_remaining = 8 - (speech_bit_pos % 8);
			buf[idx] |= amr_ac->enc_arr[i] >> (8 - bits_remaining);
			speech_bit_pos += bits_remaining;
			if (speech_bit_pos < eof_bit_pos) {
				buf[idx+1] = (amr_ac->enc_arr[i] & ((1 << (8-bits_remaining))-1))<<bits_remaining;
			}
			speech_bit_pos += (8-bits_remaining);
		}
		speech_bit_pos = eof_bit_pos;

		p_in += samps_per_frame;
		frame_num++;
	}

	*len = (speech_bit_pos+7)/8;
	return 0;
}

static int encode_handler(struct auenc_state *st, bool *marker, uint8_t *buf, size_t *len, int fmt, const void *sampv, size_t sampc)
{
	const struct amr_aucodec *amr_ac;
	const int16_t *p = sampv;
	const int * frame_size_tbl;
	uint32_t num_frames, frame_idx, samps_per_frame;
	int n;
	(void)marker;

	if (!st || !buf || !len || !sampv)
		return EINVAL;

	if (fmt != AUFMT_S16LE)
		return ENOTSUP;

	amr_ac = (struct amr_aucodec *)st->ac;

	switch (amr_ac->ac.srate) {
#ifdef AMR_NB
	case 8000:
		samps_per_frame = NB_SAMPS_PER_FRAME;
		frame_size_tbl = frame_size_nb;
		break;
#endif

#ifdef AMR_WB
	case 16000:
		samps_per_frame = WB_SAMPS_PER_FRAME;
		frame_size_tbl = frame_size_wb;
		break;
#endif
	default:
		samps_per_frame = 0;
		frame_size_tbl = frame_size_nb;
	}

	if (!samps_per_frame || sampc % samps_per_frame)
		return EINVAL;

	num_frames = sampc / samps_per_frame;
	frame_idx = 0;

	if (amr_ac->aligned) {
		/* Speech data starts after CMR header and TOC bytes for each frame */
		uint32_t payload_idx = 1 + num_frames;

		/* CMR value 15 indicates that no mode request is present */
		buf[0] = 15 << 4;
		*len = 1;
		while (frame_idx < num_frames) {
			n = encode_wrapper(st, p);
			if (n <= 0)
				return EPROTO;
			/* Move header byte into TOC and set F bit if more frames are expected */
			if (frame_idx == num_frames-1)
				buf[1+frame_idx] = amr_ac->enc_arr[0];
			else
				buf[1+frame_idx] = amr_ac->enc_arr[0] | 0x80;
			frame_idx++;
			memcpy(&buf[payload_idx], &amr_ac->enc_arr[1], n);
			payload_idx += (n-1);
			*len += n;
			p += samps_per_frame;
		}
		return 0;
	}
	else {
		return encode_be(st, p, num_frames, samps_per_frame, frame_size_tbl, buf, len);
	}
}

static int decode_handler(struct audec_state *st, int fmt, void *sampv,
		     size_t *sampc,
		     bool marker, const uint8_t *buf, size_t len)
{
	const struct amr_aucodec *amr_ac;
	const int * frame_size_tbl;
	int16_t *p = sampv;
	uint32_t frame_num, frame_cnt, samps_per_frame;
	(void)marker;

	if (!st || !sampv || !sampc || !buf)
		return EINVAL;

	amr_ac = (struct amr_aucodec *)st->ac;

	switch (amr_ac->ac.srate) {
#ifdef AMR_NB
	case 8000:
		samps_per_frame = NB_SAMPS_PER_FRAME;
		frame_size_tbl = frame_size_nb;
		break;
#endif

#ifdef AMR_WB
	case 16000:
		samps_per_frame = WB_SAMPS_PER_FRAME;
		frame_size_tbl = frame_size_wb;
		break;
#endif
	default:
		samps_per_frame = 0;
		frame_size_tbl = frame_size_nb;
	}

	if (!samps_per_frame)
		return EINVAL;

	if (fmt != AUFMT_S16LE)
		return ENOTSUP;

	frame_num = 0;
	frame_cnt = 0;
	*sampc = 0;

	if (amr_ac->aligned) {
		uint32_t payload_idx;
		/* Iterate through all the TOC bytes to count the number of frames. First byte is CMR so skip that. */
		while( (buf[1+(frame_cnt++)] & 0x80) && frame_cnt < len-1);
		/* First frame payload starts after the TOC. */
		payload_idx = 1+frame_cnt;
		while (frame_num < frame_cnt) {
			/* Extract frame type */
			uint8_t FT = (buf[1+frame_num] >> 3) & 0x0F;
			if (FT >= 16 || frame_size_tbl[FT] < 0 || (frame_size_tbl[FT]+7)/8 + payload_idx > len)
				return EPROTO;
			/* Opencore AMR decoder wrapper expects frame type in first byte followed by AMR speech frame */
			amr_ac->dec_arr[0] = buf[1 + frame_num];
			memcpy(&amr_ac->dec_arr[1], &buf[payload_idx], (frame_size_nb[FT]+7)/8);
			decode_wrapper(st, p);
			p += samps_per_frame;
			*sampc += samps_per_frame;
			frame_num++;
			payload_idx += (frame_size_tbl[FT]+7)/8;
		}
		return 0;
	} else {
		return decode_be(st, buf, len, samps_per_frame, frame_size_tbl, sampv, sampc);
	}
}

#ifdef AMR_WB
static struct amr_aucodec amr_wb = {
	.ac = {
		.name      = "AMR-WB",
		.srate     = 16000,
		.crate     = 16000,
		.ch        = 1,
		.pch       = 1,
		.encupdh   = encode_update,
		.ench      = encode_handler,
		.decupdh   = decode_update,
		.dech      = decode_handler,
		.fmtp_ench = amr_fmtp_enc
	},
	.aligned = false,
	.dec_arr = NULL,
	.enc_arr = NULL
};
#endif
#ifdef AMR_NB
static struct amr_aucodec amr_nb = {
	.ac = {
		.name      = "AMR",
		.srate     = 8000,
		.crate     = 8000,
		.ch        = 1,
		.pch       = 1,
		.encupdh   = encode_update,
		.ench      = encode_handler,
		.decupdh   = decode_update,
		.dech      = decode_handler,
		.fmtp_ench = amr_fmtp_enc
	},
	.aligned = false,
	.dec_arr = NULL,
	.enc_arr = NULL
};
#endif


static int module_init(void)
{
	int err = 0;

#ifdef AMR_WB
	aucodec_register(baresip_aucodecl(), (struct aucodec *) &amr_wb);
#endif
#ifdef AMR_NB
	aucodec_register(baresip_aucodecl(), (struct aucodec *) &amr_nb);
#endif

	return err;
}


static int module_close(void)
{
#ifdef AMR_WB
	aucodec_unregister((struct aucodec *) &amr_wb);
#endif
#ifdef AMR_NB
	aucodec_unregister((struct aucodec *) &amr_nb);
#endif

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(amr) = {
	"amr",
	"codec",
	module_init,
	module_close
};
