/**
 * @file g722.c  G.722 audio codec
 *
 * Copyright (C) 2010 Alfred E. Heggestad
 */
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <re.h>
#include <rem_au.h>
#include <baresip.h>

/* Forward declarations */
static void encode_destructor(void *arg);
static void decode_destructor(void *arg);

#ifdef USE_SPANDSP
#define SPANDSP_EXPOSE_INTERNAL_STRUCTURES 1
#include <spandsp.h>
#endif

#ifdef USE_LIBG722
#include <g722_codec.h>
#endif

#if !defined(USE_SPANDSP) && !defined(USE_LIBG722)
#error "Neither SPANDSP nor libg722 is available. Please install one of them."
#endif

/**
 * @defgroup g722 g722
 *
 * The G.722 audio codec
 *
 * This module supports both SPANDSP and libg722 libraries.
 * SPANDSP is preferred if both are available.
 *
 * ## From RFC 3551:

 4.5.2 G722

   G722 is specified in ITU-T Recommendation G.722, "7 kHz audio-coding
   within 64 kbit/s".  The G.722 encoder produces a stream of octets,
   each of which SHALL be octet-aligned in an RTP packet.  The first bit
   transmitted in the G.722 octet, which is the most significant bit of
   the higher sub-band sample, SHALL correspond to the most significant
   bit of the octet in the RTP packet.

   Even though the actual sampling rate for G.722 audio is 16,000 Hz,
   the RTP clock rate for the G722 payload format is 8,000 Hz because
   that value was erroneously assigned in RFC 1890 and must remain
   unchanged for backward compatibility.  The octet rate or sample-pair
   rate is 8,000 Hz.

   ##   References:

  http://www.soft-switch.org/spandsp-modules.html
  https://github.com/pschatzmann/libg722

 */


enum {
	G722_SAMPLE_RATE = 16000,
	G722_BITRATE_48k = 48000,
	G722_BITRATE_56k = 56000,
	G722_BITRATE_64k = 64000
};


struct auenc_state {
#ifdef USE_SPANDSP
	g722_encode_state_t enc;
#endif
#ifdef USE_LIBG722
	G722_ENC_CTX *enc;
#endif
};

struct audec_state {
#ifdef USE_SPANDSP
	g722_decode_state_t dec;
#endif
#ifdef USE_LIBG722
	G722_DEC_CTX *dec;
#endif
};


static int encode_update(struct auenc_state **aesp,
			 const struct aucodec *ac,
			 struct auenc_param *prm, const char *fmtp)
{
	struct auenc_state *st;
	int err = 0;
	(void)prm;
	(void)fmtp;

	if (!aesp || !ac)
		return EINVAL;

	if (*aesp)
		return 0;

	st = mem_alloc(sizeof(*st), encode_destructor);
	if (!st)
		return ENOMEM;

#ifdef USE_SPANDSP
	if (!g722_encode_init(&st->enc, G722_BITRATE_64k, 0)) {
		err = EPROTO;
		goto out;
	}
#endif

#ifdef USE_LIBG722
	st->enc = g722_encoder_new(G722_BITRATE_64k, 0);
	if (!st->enc) {
		err = EPROTO;
		goto out;
	}
#endif

 out:
	if (err)
		mem_deref(st);
	else
		*aesp = st;

	return err;
}


static int decode_update(struct audec_state **adsp,
			 const struct aucodec *ac, const char *fmtp)
{
	struct audec_state *st;
	int err = 0;
	(void)fmtp;

	if (!adsp || !ac)
		return EINVAL;

	if (*adsp)
		return 0;

	st = mem_alloc(sizeof(*st), decode_destructor);
	if (!st)
		return ENOMEM;

#ifdef USE_SPANDSP
	if (!g722_decode_init(&st->dec, G722_BITRATE_64k, 0)) {
		err = EPROTO;
		goto out;
	}
#endif

#ifdef USE_LIBG722
	st->dec = g722_decoder_new(G722_BITRATE_64k, 0);
	if (!st->dec) {
		err = EPROTO;
		goto out;
	}
#endif

 out:
	if (err)
		mem_deref(st);
	else
		*adsp = st;

	return err;
}


static int encode(struct auenc_state *st,
		  bool *marker, uint8_t *buf, size_t *len,
		  int fmt, const void *sampv, size_t sampc)
{
	int n;
	(void)marker;

	if (!st)
		return EINVAL;

	if (fmt != AUFMT_S16LE)
		return ENOTSUP;

#ifdef USE_SPANDSP
	n = g722_encode(&st->enc, buf, sampv, (int)sampc);
#endif

#ifdef USE_LIBG722
	if (!st->enc)
		return EINVAL;
	n = g722_encode(st->enc, (const int16_t *)sampv, (int)sampc, buf);
#endif

	if (n <= 0) {
		return EPROTO;
	}
	else if (n > (int)*len) {
		return EOVERFLOW;
	}

	*len = n;

	return 0;
}


static int decode(struct audec_state *st, int fmt, void *sampv, size_t *sampc,
		  bool marker, const uint8_t *buf, size_t len)
{
	int n;
	(void)marker;

	if (!st || !sampv || !buf)
		return EINVAL;

	if (fmt != AUFMT_S16LE)
		return ENOTSUP;

#ifdef USE_SPANDSP
	n = g722_decode(&st->dec, sampv, buf, (int)len);
#endif

#ifdef USE_LIBG722
	if (!st->dec)
		return EINVAL;
	n = g722_decode(st->dec, buf, (int)len, (int16_t *)sampv);
#endif

	if (n < 0)
		return EPROTO;

	*sampc = n;

	return 0;
}


static void encode_destructor(void *arg)
{
	struct auenc_state *st = arg;
	if (!st)
		return;

#ifdef USE_LIBG722
	if (st->enc) {
		g722_encoder_destroy(st->enc);
		st->enc = NULL;
	}
#endif
}


static void decode_destructor(void *arg)
{
	struct audec_state *st = arg;
	if (!st)
		return;

#ifdef USE_LIBG722
	if (st->dec) {
		g722_decoder_destroy(st->dec);
		st->dec = NULL;
	}
#endif
}


static struct aucodec g722 = {
	.pt      = "9",
	.name    = "G722",
	.srate   = 16000,
	.crate   = 8000,
	.ch      = 1,
	.pch     = 1,
	.encupdh = encode_update,
	.ench    = encode,
	.decupdh = decode_update,
	.dech    = decode,
};


static int module_init(void)
{
#ifdef USE_SPANDSP
	info("g722: using SPANDSP library\n");
#elif USE_LIBG722
	info("g722: using libg722 library\n");
#else
	warning("g722: no G.722 library available\n");
	return ENOSYS;
#endif

	aucodec_register(baresip_aucodecl(), &g722);
	return 0;
}


static int module_close(void)
{
	aucodec_unregister(&g722);
	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(g722) = {
	"g722",
	"codec",
	module_init,
	module_close
};
