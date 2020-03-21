/**
 * @file vumeter.c  VU-meter
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <string.h>
#include <stdlib.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>


/**
 * @defgroup vumeter vumeter
 *
 * Simple ASCII VU-meter for the audio-signal.
 *
 * The Volume unit (VU) meter module takes the audio-signal as input
 * and prints a simple ASCII-art bar for the recording and playback levels.
 * It is using the aufilt API to get the audio samples.
 */


struct vumeter_enc {
	struct aufilt_enc_st af;  /* inheritance */
	struct tmr tmr;
	const struct audio *au;
	double avg_rec;
	volatile bool started;
	enum aufmt fmt;
};

struct vumeter_dec {
	struct aufilt_dec_st af;  /* inheritance */
	struct tmr tmr;
	const struct audio *au;
	double avg_play;
	volatile bool started;
	enum aufmt fmt;
};


static bool vumeter_stderr;


static void send_event(const struct audio *au, enum ua_event ev, double value)
{
	audio_level_put(au, ev == UA_EVENT_VU_TX, value);
}


static void enc_destructor(void *arg)
{
	struct vumeter_enc *st = arg;

	list_unlink(&st->af.le);
	tmr_cancel(&st->tmr);
}


static void dec_destructor(void *arg)
{
	struct vumeter_dec *st = arg;

	list_unlink(&st->af.le);
	tmr_cancel(&st->tmr);
}


static int audio_print_vu(struct re_printf *pf, double *level)
{
	char buf[16];
	size_t res;
	double x;

	x = (*level + -AULEVEL_MIN) / -AULEVEL_MIN;

	res = min((size_t)(sizeof(buf) * x),
		  sizeof(buf)-1);

	memset(buf, '=', res);
	buf[res] = '\0';

	return re_hprintf(pf, "[%-16s]", buf);
}


static void print_vumeter(int pos, int color, double value)
{
	/* move cursor to a fixed position */
	re_fprintf(stderr, "\x1b[%dG", pos);

	/* print VU-meter in Nice colors */
	re_fprintf(stderr, " \x1b[%dm%H\x1b[;m\r",
		   color, audio_print_vu, &value);
}


static void enc_tmr_handler(void *arg)
{
	struct vumeter_enc *st = arg;

	tmr_start(&st->tmr, 500, enc_tmr_handler, st);

	if (st->started) {
		if (vumeter_stderr)
			print_vumeter(60, 31, st->avg_rec);

		send_event(st->au, UA_EVENT_VU_TX, st->avg_rec);
	}
}


static void dec_tmr_handler(void *arg)
{
	struct vumeter_dec *st = arg;

	tmr_start(&st->tmr, 500, dec_tmr_handler, st);

	if (st->started) {
		if (vumeter_stderr)
			print_vumeter(80, 32, st->avg_play);

		send_event(st->au, UA_EVENT_VU_RX, st->avg_play);
	}
}


static int encode_update(struct aufilt_enc_st **stp, void **ctx,
			 const struct aufilt *af, struct aufilt_prm *prm,
			 const struct audio *au)
{
	struct vumeter_enc *st;
	(void)ctx;
	(void)prm;

	if (!stp || !af || !prm)
		return EINVAL;

	if (*stp)
		return 0;

	st = mem_zalloc(sizeof(*st), enc_destructor);
	if (!st)
		return ENOMEM;

	st->au = au;
	st->fmt = prm->fmt;
	tmr_start(&st->tmr, 100, enc_tmr_handler, st);

	*stp = (struct aufilt_enc_st *)st;

	return 0;
}


static int decode_update(struct aufilt_dec_st **stp, void **ctx,
			 const struct aufilt *af, struct aufilt_prm *prm,
			 const struct audio *au)
{
	struct vumeter_dec *st;
	(void)ctx;
	(void)prm;

	if (!stp || !af || !prm)
		return EINVAL;

	if (*stp)
		return 0;

	st = mem_zalloc(sizeof(*st), dec_destructor);
	if (!st)
		return ENOMEM;

	st->au = au;
	st->fmt = prm->fmt;
	tmr_start(&st->tmr, 100, dec_tmr_handler, st);

	*stp = (struct aufilt_dec_st *)st;

	return 0;
}


static int encode(struct aufilt_enc_st *st, struct auframe *af)
{
	struct vumeter_enc *vu = (void *)st;

	if (!st || !af)
		return EINVAL;

	vu->avg_rec = aulevel_calc_dbov(vu->fmt, af->sampv, af->sampc);
	vu->started = true;

	return 0;
}


static int decode(struct aufilt_dec_st *st, struct auframe *af)
{
	struct vumeter_dec *vu = (void *)st;

	if (!st || !af)
		return EINVAL;

	vu->avg_play = aulevel_calc_dbov(vu->fmt, af->sampv, af->sampc);
	vu->started = true;

	return 0;
}


static struct aufilt vumeter = {
	.name    = "vumeter",
	.encupdh = encode_update,
	.ench    = encode,
	.decupdh = decode_update,
	.dech    = decode
};


static int module_init(void)
{
	struct conf *conf = conf_cur();

	conf_get_bool(conf, "vumeter_stderr", &vumeter_stderr);

	aufilt_register(baresip_aufiltl(), &vumeter);

	return 0;
}


static int module_close(void)
{
	aufilt_unregister(&vumeter);
	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(vumeter) = {
	"vumeter",
	"filter",
	module_init,
	module_close
};
