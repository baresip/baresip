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
#include "fvad.h"


/**
 * @defgroup fvad fvad
 *
 * Voice Activity Detection for the audio-signal.
 *
 * It is using the aufilt API to get the audio samples.
 */


struct vad_enc {
	struct aufilt_enc_st af;  /* inheritance */
	bool vad_tx;
	Fvad *fvad;
	struct call *call;
};


struct vad_dec {
	struct aufilt_enc_st af;  /* inheritance */
	bool vad_rx;
	Fvad *fvad;
	struct call *call;
};

struct filter_arg {
	const struct audio *audio;
	struct call *call;
};

static bool vad_stderr = false;


static void enc_destructor(void *arg)
{
	struct vad_enc *st = arg;

	if (st->fvad) {
		fvad_free(st->fvad);
	}

	list_unlink(&st->af.le);
}


static void dec_destructor(void *arg)
{
	struct vad_dec *st = arg;

	if (st->fvad) {
		fvad_free(st->fvad);
	}

	list_unlink(&st->af.le);
}


static void print_vad(int pos, int color, bool tx, bool active)
{
	/* move cursor to a fixed position */
	re_fprintf(stderr, "\x1b[%dG", pos);

	/* print vad in Nice colors */
	re_fprintf(stderr, " \x1b[%dm[%s]\x1b[;m\r",
		   color,  active ? (tx ? "tx" : "rx") : "  ");
}


static void find_first_call(struct call *call, void *arg)
{
	struct filter_arg *fa = arg;

	if (!fa->call)
		fa->call = call;
}


static bool find_call(const struct call *call, void *arg)
{
	struct filter_arg *fa = arg;

	return call_audio(call) == fa->audio;
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

	if (!st->call) {
		struct filter_arg fa = { au, NULL };

		uag_filter_calls(find_first_call, find_call, &fa);
		st->call = fa.call;
	}

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

	if (!st->call) {
		struct filter_arg fa = { au, NULL };

		uag_filter_calls(find_first_call, find_call, &fa);
		st->call = fa.call;
	}

	*stp = (struct aufilt_dec_st *)st;

	return 0;
}


static bool auframe_vad(Fvad *fvad, struct auframe *af)
{
	static int chunk_times_ms[] = { 30, 20, 10 };

	if (af->fmt != AUFMT_S16LE) {
		warning("fvad: invalid sample format %d\n",
			af->fmt);

		return false;
	}

	size_t pos = 0;

	/* process all chunk_sizes that fvad accepts */
	for (size_t chunk_time_index = 0;
		chunk_time_index < RE_ARRAY_SIZE(chunk_times_ms);
		++chunk_time_index) {

		const size_t chunk_time = chunk_times_ms[chunk_time_index];
		size_t sampc = af->srate / 1000 * chunk_time;

		while (af->sampc - pos >= sampc) {

			int err = fvad_process(fvad, (int16_t*)af->sampv + pos, sampc);
			pos += sampc;
			if (err > 0) {
				return true;
			}
			else if (err < 0) {
				warning("fvad: fvad_process(%d) failed\n",
					sampc);
				return false;
			}
		}
	}

	if (pos != af->sampc) {
		warning("fvad: fvad_process: samples left over: %d\n",
			af->sampc - pos);
	}

	return false;
}


static int encode(struct aufilt_enc_st *st, struct auframe *af)
{
	struct vad_enc *vad = (void *)st;

	if (!st || !af)
		return EINVAL;

	bool vad_tx = auframe_vad(vad->fvad, af);

	if (vad_tx != vad->vad_tx) {
		vad->vad_tx = vad_tx;

		if (vad_stderr)
			print_vad(61, 32, false, vad_tx);

		module_event("fvad", "vad", call_get_ua(vad->call), vad->call,
			"%d", vad_tx);
	}

	return 0;
}


static int decode(struct aufilt_dec_st *st, struct auframe *af)
{
	struct vad_dec *vad = (void *)st;

	if (!st || !af)
		return EINVAL;

	bool vad_rx = auframe_vad(vad->fvad, af);

	if (vad_rx != vad->vad_rx) {
		vad->vad_rx = vad_rx;

		if (vad_stderr)
			print_vad(64, 32, false, vad_rx);

		module_event("fvad", "vad", call_get_ua(vad->call),
			(struct call*)vad->call, "%d", vad_rx);
	}

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

	conf_get_bool(conf, "fvad_stderr", &vad_stderr);

	bool rx_enabled = true;
	conf_get_bool(conf, "fvad_rx", &rx_enabled);

	bool tx_enabled = true;
	conf_get_bool(conf, "fvad_tx", &tx_enabled);

	if (!rx_enabled) {
		vad.dech = NULL;
		vad.decupdh = NULL;
	}

	if (!tx_enabled) {
		vad.ench = NULL;
		vad.encupdh = NULL;
	}

	if (!tx_enabled && !rx_enabled) {
		warning("fvad: neither fvad_rx nor fvad_tx are enabled"
			", not loading filter\n");
		return 0;
	}

	aufilt_register(baresip_aufiltl(), &vad);

	return 0;
}


static int module_close(void)
{
	if (vad.dech || vad.ench)
		aufilt_unregister(&vad);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(vad) = {
	"vad",
	"filter",
	module_init,
	module_close
};
