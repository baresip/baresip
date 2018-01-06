/**
 * @file speex.c  Speex audio codec
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <stdlib.h>
#include <speex/speex.h>
#include <speex/speex_stereo.h>
#include <speex/speex_callbacks.h>
#include <re.h>
#include <baresip.h>


/**
 * @defgroup speex speex
 *
 * The Speex audio codec
 *
 * NOTE: The Speex codec has been obsoleted by Opus.
 */


enum {
	MIN_FRAME_SIZE = 43,
	SPEEX_PTIME    = 20,
};


struct auenc_state {
	void *enc;
	SpeexBits bits;

	uint32_t frame_size;  /* Number of sample-frames */
	uint8_t channels;
};


struct audec_state {
	void *dec;
	SpeexBits bits;
	SpeexStereoState stereo;
	SpeexCallback callback;

	uint32_t frame_size;  /* Number of sample-frames */
	uint8_t channels;
};


static char speex_fmtp_nb[128];
static char speex_fmtp_wb[128];


/** Speex configuration */
static struct {
	int quality;
	int complexity;
	int enhancement;
	int mode_nb;
	int mode_wb;
	int vbr;
	int vad;
} sconf = {
	3,  /* 0-10   */
	2,  /* 0-10   */
	0,  /* 0 or 1 */
	3,  /* 1-6    */
	6,  /* 1-6    */
	0,  /* 0 or 1 */
	0   /* 0 or 1 */
};


static void encode_destructor(void *arg)
{
	struct auenc_state *st = arg;

	speex_bits_destroy(&st->bits);
	speex_encoder_destroy(st->enc);
}


static void decode_destructor(void *arg)
{
	struct audec_state *st = arg;

	speex_bits_destroy(&st->bits);
	speex_decoder_destroy(st->dec);
}


static void encoder_config(void *st)
{
	int ret;

	ret = speex_encoder_ctl(st, SPEEX_SET_QUALITY, &sconf.quality);
	if (ret) {
		warning("speex: SPEEX_SET_QUALITY: %d\n", ret);
	}

	ret = speex_encoder_ctl(st, SPEEX_SET_COMPLEXITY, &sconf.complexity);
	if (ret) {
		warning("speex: SPEEX_SET_COMPLEXITY: %d\n", ret);
	}

	ret = speex_encoder_ctl(st, SPEEX_SET_VBR, &sconf.vbr);
	if (ret) {
		warning("speex: SPEEX_SET_VBR: %d\n", ret);
	}

	ret = speex_encoder_ctl(st, SPEEX_SET_VAD, &sconf.vad);
	if (ret) {
		warning("speex: SPEEX_SET_VAD: %d\n", ret);
	}
}


static void decoder_config(void *st)
{
	int ret;

	ret = speex_decoder_ctl(st, SPEEX_SET_ENH, &sconf.enhancement);
	if (ret) {
		warning("speex: SPEEX_SET_ENH: %d\n", ret);
	}
}


static int decode_param(struct auenc_state *st, const struct pl *name,
			const struct pl *val)
{
	int ret;

	/* mode: List supported Speex decoding modes.  The valid modes are
	   different for narrowband and wideband, and are defined as follows:

	   {1,2,3,4,5,6,any}
	 */
	if (0 == pl_strcasecmp(name, "mode")) {
		struct pl v;
		int mode;

		/* parameter is quoted */
		if (re_regex(val->p, val->l, "\"[^\"]+\"", &v))
			v = *val;

		if (0 == pl_strcasecmp(&v, "any"))
			return 0;

		mode = pl_u32(&v);

		ret = speex_encoder_ctl(st->enc, SPEEX_SET_MODE, &mode);
		if (ret) {
			warning("speex: SPEEX_SET_MODE: ret=%d\n", ret);
		}
	}
	/* vbr: variable bit rate - either 'on' 'off' or 'vad' */
	else if (0 == pl_strcasecmp(name, "vbr")) {
		int vbr = 0, vad = 0;

		if (0 == pl_strcasecmp(val, "on"))
			vbr = 1;
		else if (0 == pl_strcasecmp(val, "off"))
			vbr = 0;
		else if (0 == pl_strcasecmp(val, "vad"))
			vad = 1;
		else {
			warning("speex: invalid vbr value %r\n", val);
		}

		debug("speex: setting VBR=%d VAD=%d\n", vbr, vad);
		ret = speex_encoder_ctl(st->enc, SPEEX_SET_VBR, &vbr);
		if (ret) {
			warning("speex: SPEEX_SET_VBR: ret=%d\n", ret);
		}
		ret = speex_encoder_ctl(st->enc, SPEEX_SET_VAD, &vad);
		if (ret) {
			warning("speex: SPEEX_SET_VAD: ret=%d\n", ret);
		}
	}
	else if (0 == pl_strcasecmp(name, "cng")) {
		int dtx = 0;

		if (0 == pl_strcasecmp(val, "on"))
			dtx = 0;
		else if (0 == pl_strcasecmp(val, "off"))
			dtx = 1;

		ret = speex_encoder_ctl(st->enc, SPEEX_SET_DTX, &dtx);
		if (ret) {
			warning("speex: SPEEX_SET_DTX: ret=%d\n", ret);
		}
	}
	else {
		debug("speex: unknown Speex param: %r=%r\n", name, val);
	}

	return 0;
}


static void param_handler(const struct pl *name, const struct pl *val,
			  void *arg)
{
	struct auenc_state *st = arg;

	decode_param(st, name, val);
}


static const SpeexMode *resolve_mode(uint32_t srate)
{
	switch (srate) {

	default:
	case 8000:  return &speex_nb_mode;
	case 16000: return &speex_wb_mode;
	case 32000: return &speex_uwb_mode;
	}
}


static int encode_update(struct auenc_state **aesp, const struct aucodec *ac,
			 struct auenc_param *prm, const char *fmtp)
{
	struct auenc_state *st;
	int ret, err = 0;

	if (!aesp || !ac || !prm)
		return EINVAL;
	if (prm->ptime != SPEEX_PTIME)
		return EPROTO;
	if (*aesp)
		return 0;

	st = mem_zalloc(sizeof(*st), encode_destructor);
	if (!st)
		return ENOMEM;

	st->frame_size = ac->srate * SPEEX_PTIME / 1000;
	st->channels = ac->ch;

	/* Encoder */
	st->enc = speex_encoder_init(resolve_mode(ac->srate));
	if (!st->enc) {
		err = ENOMEM;
		goto out;
	}

	speex_bits_init(&st->bits);

	encoder_config(st->enc);

	ret = speex_encoder_ctl(st->enc, SPEEX_GET_FRAME_SIZE,
				&st->frame_size);
	if (ret) {
		warning("speex: SPEEX_GET_FRAME_SIZE: %d\n", ret);
	}

	if (str_isset(fmtp)) {
		struct pl params;

		pl_set_str(&params, fmtp);

		fmt_param_apply(&params, param_handler, st);
	}

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

	st = mem_zalloc(sizeof(*st), decode_destructor);
	if (!st)
		return ENOMEM;

	st->frame_size = ac->srate * SPEEX_PTIME / 1000;
	st->channels = ac->ch;

	/* Decoder */
	st->dec = speex_decoder_init(resolve_mode(ac->srate));
	if (!st->dec) {
		err = ENOMEM;
		goto out;
	}

	speex_bits_init(&st->bits);

	if (2 == st->channels) {

		/* Stereo. */
		st->stereo.balance = 1;
		st->stereo.e_ratio = .5f;
		st->stereo.smooth_left = 1;
		st->stereo.smooth_right = 1;

		st->callback.callback_id = SPEEX_INBAND_STEREO;
		st->callback.func = speex_std_stereo_request_handler;
		st->callback.data = &st->stereo;
		speex_decoder_ctl(st->dec, SPEEX_SET_HANDLER,
				  &st->callback);
	}

	decoder_config(st->dec);

 out:
	if (err)
		mem_deref(st);
	else
		*adsp = st;

	return err;
}


static int encode(struct auenc_state *st, uint8_t *buf,
		  size_t *len, const int16_t *sampv, size_t sampc)
{
	const size_t n = st->channels * st->frame_size;
	int ret, r;

	if (*len < 128)
		return ENOMEM;

	/* VAD */
	if (!sampv || !sampc) {
		/* 5 zeros interpreted by Speex as silence (submode 0) */
		speex_bits_pack(&st->bits, 0, 5);
		goto out;
	}

	/* Handle multiple Speex frames in one RTP packet */
	while (sampc > 0) {

		/* Assume stereo */
		if (2 == st->channels) {
			speex_encode_stereo_int((int16_t *)sampv,
						st->frame_size, &st->bits);
		}

		ret = speex_encode_int(st->enc, (int16_t *)sampv, &st->bits);
		if (1 != ret) {
			warning("speex: speex_encode_int: ret=%d\n", ret);
		}

		sampc -= n;
		sampv += n;
	}

 out:
	/* Terminate bit stream */
	speex_bits_pack(&st->bits, 15, 5);

	r = speex_bits_write(&st->bits, (char *)buf, (int)*len);
	*len = r;

	speex_bits_reset(&st->bits);

	return 0;
}


static int decode(struct audec_state *st, int16_t *sampv,
		  size_t *sampc, const uint8_t *buf, size_t len)
{
	const size_t n = st->channels * st->frame_size;
	size_t i = 0;

	/* Read into bit-stream */
	speex_bits_read_from(&st->bits, (char *)buf, (int)len);

	/* Handle multiple Speex frames in one RTP packet */
	while (speex_bits_remaining(&st->bits) >= MIN_FRAME_SIZE) {
		int ret;

		if (*sampc < n)
			return ENOMEM;

		ret = speex_decode_int(st->dec, &st->bits,
				       (int16_t *)&sampv[i]);
		if (ret < 0) {
			if (-1 == ret) {
			}
			else if (-2 == ret) {
				warning("speex: decode: corrupt stream\n");
			}
			else {
				warning("speex: decode: speex_decode_int:"
					" ret=%d\n", ret);
			}
			break;
		}

		/* Transforms a mono frame into a stereo frame
		   using intensity stereo info */
		if (2 == st->channels) {
			speex_decode_stereo_int((int16_t *)&sampv[i],
						st->frame_size,
						&st->stereo);
		}

		i      += n;
		*sampc -= n;
	}

	*sampc = i;

	return 0;
}


static int pkloss(struct audec_state *st, int16_t *sampv, size_t *sampc)
{
	const size_t n = st->channels * st->frame_size;

	if (*sampc < n)
		return ENOMEM;

	/* Silence */
	speex_decode_int(st->dec, NULL, sampv);
	*sampc = n;

	return 0;
}


static void config_parse(struct conf *conf)
{
	uint32_t v;

	if (0 == conf_get_u32(conf, "speex_quality", &v))
		sconf.quality = v;
	if (0 == conf_get_u32(conf, "speex_complexity", &v))
		sconf.complexity = v;
	if (0 == conf_get_u32(conf, "speex_enhancement", &v))
		sconf.enhancement = v;
	if (0 == conf_get_u32(conf, "speex_mode_nb", &v))
		sconf.mode_nb = v;
	if (0 == conf_get_u32(conf, "speex_mode_wb", &v))
		sconf.mode_wb = v;
	if (0 == conf_get_u32(conf, "speex_vbr", &v))
		sconf.vbr = v;
	if (0 == conf_get_u32(conf, "speex_vad", &v))
		sconf.vad = v;
}


static struct aucodec speexv[] = {

	/* Stereo Speex */
	{LE_INIT, 0, "speex", 32000, 32000, 2, speex_fmtp_wb,
	 encode_update, encode, decode_update, decode, pkloss, 0, 0, 0, 0},
	{LE_INIT, 0, "speex", 16000, 16000, 2, speex_fmtp_wb,
	 encode_update, encode, decode_update, decode, pkloss, 0, 0, 0, 0},
	{LE_INIT, 0, "speex",  8000,  8000, 2, speex_fmtp_nb,
	 encode_update, encode, decode_update, decode, pkloss, 0, 0, 0, 0},

	/* Standard Speex */
	{LE_INIT, 0, "speex", 32000, 32000, 1, speex_fmtp_wb,
	 encode_update, encode, decode_update, decode, pkloss, 0, 0, 0, 0},
	{LE_INIT, 0, "speex", 16000, 16000, 1, speex_fmtp_wb,
	 encode_update, encode, decode_update, decode, pkloss, 0, 0, 0, 0},
	{LE_INIT, 0, "speex",  8000,  8000, 1, speex_fmtp_nb,
	 encode_update, encode, decode_update, decode, pkloss, 0, 0, 0, 0},
};


static int speex_init(void)
{
	size_t i;

	config_parse(conf_cur());

	(void)re_snprintf(speex_fmtp_nb, sizeof(speex_fmtp_nb),
			  "mode=\"%d\";vbr=%s;cng=on", sconf.mode_nb,
			  sconf.vad ? "vad" : (sconf.vbr ? "on" : "off"));

	(void)re_snprintf(speex_fmtp_wb, sizeof(speex_fmtp_wb),
			  "mode=\"%d\";vbr=%s;cng=on", sconf.mode_wb,
			  sconf.vad ? "vad" : (sconf.vbr ? "on" : "off"));

	for (i=0; i<ARRAY_SIZE(speexv); i++)
		aucodec_register(baresip_aucodecl(), &speexv[i]);

	return 0;
}


static int speex_close(void)
{
	size_t i;
	for (i=0; i<ARRAY_SIZE(speexv); i++)
		aucodec_unregister(&speexv[i]);
	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(speex) = {
	"speex",
	"codec",
	speex_init,
	speex_close
};
