/**
 * @file fvad.c  Voice Activity Detection
 *
 * Uses libfvad from https://github.com/dpirch/libfvad
 *
 * Copyright (C) 2023 Lars Immisch
 */
#include <string.h>
#include <stdlib.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <fvad.h>


/**
 * @defgroup fvad fvad
 *
 * Voice Activity Detection for the audio-signal.
 *
 * It is using the aufilt API to get the audio samples.
 */


struct vad_enc {
	struct aufilt_enc_st af;  /* inheritance */
	struct tmr tmr;
	const struct audio *au;
	bool vad_tx;
	volatile bool started;
	Fvad *fvad;
};


struct vad_dec {
	struct aufilt_enc_st af;  /* inheritance */
	struct tmr tmr;
	const struct audio *au;
	bool vad_rx;
	volatile bool started;
	Fvad *fvad;
};


static bool vad_stderr = false;


static void enc_destructor(void *arg)
{
	struct vad_enc *st = arg;

	if (st->fvad) {
		fvad_free(st->fvad);
	}

	list_unlink(&st->af.le);
	tmr_cancel(&st->tmr);
}


static void dec_destructor(void *arg)
{
	struct vad_enc *st = arg;

	if (st->fvad) {
		fvad_free(st->fvad);
	}

	list_unlink(&st->af.le);
	tmr_cancel(&st->tmr);
}


static int audio_print_vad(struct re_printf *pf, bool tx, bool voice)
{
	return re_hprintf(pf, "[%s %s]", tx ? "tx" : "rx", voice ? "x" : " ");
}


static void print_vad(int pos, int color, bool tx, bool active)
{
	/* move cursor to a fixed position */
	re_fprintf(stderr, "\x1b[%dG", pos);

	/* print vad in Nice colors */
	re_fprintf(stderr, " \x1b[%dm%H\x1b[;m\r",
		   color, audio_print_vad, tx, active);
}


static void enc_tmr_handler(void *arg)
{
	struct vad_enc *st = arg;

	tmr_start(&st->tmr, 500, enc_tmr_handler, st);

	if (st->started) {
		if (vad_stderr)
			print_vad(60, 31, true, st->vad_tx);

		audio_vad_put(st->au, true, st->vad_tx);
	}
}


static void dec_tmr_handler(void *arg)
{
	struct vad_dec *st = arg;

	tmr_start(&st->tmr, 500, dec_tmr_handler, st);

	if (st->started) {
		if (vad_stderr)
			print_vad(80, 32, false, st->vad_rx);

		audio_vad_put(st->au, false, st->vad_rx);
	}
}


static int encode_update(struct aufilt_enc_st **stp, void **ctx,
			 const struct aufilt *af, struct aufilt_prm *prm,
			 const struct audio *au)
{
	struct vad_enc *st;
	(void)ctx;

	if (!stp || !af || !prm)
		return EINVAL;

	if (*stp)
		return 0;

	if (prm->ch != 1)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), enc_destructor);
	if (!st)
		return ENOMEM;

	st->fvad = fvad_new();
	if (!st->fvad) {
		mem_deref(st);
		return ENOMEM;
	}

	int err = fvad_set_sample_rate(st->fvad, prm->srate);
	if (err < 0) {
		mem_deref(st);
		return EINVAL;
	}

	st->au = au;
	tmr_start(&st->tmr, 100, enc_tmr_handler, st);

	*stp = (struct aufilt_enc_st *)st;

	return 0;
}


static int decode_update(struct aufilt_dec_st **stp, void **ctx,
			 const struct aufilt *af, struct aufilt_prm *prm,
			 const struct audio *au)
{
	struct vad_dec *st;
	(void)ctx;

	if (!stp || !af || !prm)
		return EINVAL;

	if (*stp)
		return 0;

	if (prm->ch != 1)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), dec_destructor);
	if (!st)
		return ENOMEM;

	st->fvad = fvad_new();
	if (!st->fvad) {
		mem_deref(st);
		return ENOMEM;
	}

	int err = fvad_set_sample_rate(st->fvad, prm->srate);
	if (err < 0) {
		mem_deref(st);
		return EINVAL;
	}

	st->au = au;
	tmr_start(&st->tmr, 100, dec_tmr_handler, st);

	*stp = (struct aufilt_dec_st *)st;

	return 0;
}


static bool auframe_vad(Fvad *fvad, struct auframe *af)
{
	static int chunk_times_ms[] = { 30, 20, 10 };

	int16_t *buf = NULL;
	int16_t *allocated = NULL;
	bool detected = false;

	/* convert to 16 bit linear */
	if (af->fmt == AUFMT_S16LE) {
		buf = af->sampv;
	}
	else {
		allocated = (int16_t*)mem_alloc(sizeof(int16_t) * af->sampc,
			NULL);
		buf = allocated;
		auconv_to_s16(buf, af->fmt, af->sampv, af->sampc);
	}

	size_t ms = af->sampc * 1000 / af->srate;
	size_t pos = 0;

	for (size_t chunk_time_index = 0;
		/* process all chunk_sizes that fvad accepts */
		chunk_time_index < RE_ARRAY_SIZE(chunk_times_ms); ++chunk_time_index) {

		const size_t chunk_time = chunk_times_ms[chunk_time_index];
		const size_t sampc = af->srate / 1000 * chunk_time;

		for (size_t i = 0; i < ms / chunk_time; ++i) {
			int err = fvad_process(fvad, buf + pos, sampc);
			pos += sampc;
			ms -= chunk_time;
			if (err > 0) {
				detected = true;
				goto out;
			}
			else if (err < 0) {
				warning("fvad_process(%d) failed", sampc);
				goto out;
			}
		}
	}

	if (pos != af->sampc) {
		warning("fvad_process: samples left over: %d",
			af->sampc - pos);
	}

out:
	mem_deref(allocated);

	return detected;
}


static int encode(struct aufilt_enc_st *st, struct auframe *af)
{
	struct vad_enc *vu = (void *)st;

	if (!st || !af)
		return EINVAL;

	vu->vad_tx = auframe_vad(vu->fvad, af);
	vu->started = true;

	return 0;
}


static int decode(struct aufilt_dec_st *st, struct auframe *af)
{
	struct vad_dec *vu = (void *)st;

	if (!st || !af)
		return EINVAL;

	vu->vad_rx = auframe_vad(vu->fvad, af);
	vu->started = true;

	return 0;
}


static struct aufilt vad = {
	.name    = "vad",
	.encupdh = encode_update,
	.ench    = encode,
	.decupdh = decode_update,
	.dech    = decode
};


static int module_init(void)
{
	struct conf *conf = conf_cur();

	conf_get_bool(conf, "vad_stderr", &vad_stderr);

	aufilt_register(baresip_aufiltl(), &vad);

	return 0;
}


static int module_close(void)
{
	aufilt_unregister(&vad);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(vad) = {
	"vad",
	"filter",
	module_init,
	module_close
};
