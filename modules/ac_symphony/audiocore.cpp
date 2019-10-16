/**
 * @file ac_symphony.c  Commend Acoustic Echo Cancellation and Noise Reduction
 *
 * Configuration with default values:
 *
 * ac_symphony_playback_proc off   Enable if playback audio should be processed
 *                                 by audiocode.
 *
 * ac_symphony_srate       16000   At startup audiocore is initialized with
 *                                 this samplerate.
 *
 * ac_symphony_blocklen       16   Audiocore block length in milli seconds.
 *
 * Copyright (C) 2019 Commend International - Christian Spielberger
 */

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <audiocore_cwrapper.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>


/**
 * Acoustic Echo Cancellation (AEC) from Commend International
 */

struct audiocore_st {
	uint32_t samplerate;
	uint8_t  ch;           /**< Number of channels                       */
	size_t   sampc;        /**< Number of samples per frame.             */
	uint32_t nblock;       /**< Block length in samples per channel.     */

	struct aubuf *decinp;  /**< The decode data. Convert 20ms to 16ms.   */
	struct aubuf *encinp;  /**< The encode data. Convert 20ms to 16ms.   */
	struct aubuf *decout;  /**< Speaker data. Convert 16ms to 20ms.      */
	struct aubuf *encout;  /**< The send data. Convert 16ms to 20ms.     */
	int16_t *encbuf;       /**< Encode buffer for aubuf_read.            */
	int16_t *decbuf;       /**< Decode buffer for aubuf_read.            */

	AudioCoreData *ac;
	struct enc_st *est;
	struct dec_st *dst;
};

struct enc_st {
	struct aufilt_enc_st af;  /**< base class                            */
	struct aufilt_prm prm;    /**< encoding filter params                */
	size_t   sampc;           /**< Number of samples per frame.          */
	bool started;          /**< Started aec flag                         */
};

struct dec_st {
	struct aufilt_dec_st af;  /**< base class                            */
	struct aufilt_prm prm;    /**< decoding filter params                */
	size_t   sampc;           /**< Number of samples per frame.          */
};

struct audiocore_st *audiocoreState = NULL;


static void enc_destructor(void *arg)
{
	struct enc_st *st = (struct enc_st *)arg;

	if (audiocoreState && audiocoreState->est == st)
		audiocoreState->est = NULL;

	info("ac_symphony: enc_destructor\n");

	list_unlink(&st->af.le);
}


static void dec_destructor(void *arg)
{
	struct dec_st *st = (struct dec_st *)arg;

	if (audiocoreState && audiocoreState->dst == st)
		audiocoreState->dst = NULL;

	info("ac_symphony: dec_destructor\n");

	list_unlink(&st->af.le);
}


static void audiocore_st_destructor(void *arg)
{
	struct audiocore_st *st = (struct audiocore_st *)arg;

	info("ac_symphony: audiocore_st_destructor\n");

	ac_Uninit(st->ac);
	st->ac = NULL;

	mem_deref(st->decinp);
	mem_deref(st->decout);
	mem_deref(st->encinp);
	mem_deref(st->encout);

	mem_deref(st->encbuf);
	mem_deref(st->decbuf);
}


static uint32_t audiocore_nblock(uint32_t srate)
{
	/* audiocore has 16ms frames by default */
	uint32_t blocklen = 16;

	conf_get_u32(conf_cur(), "ac_symphony_blocklen", &blocklen);
	return srate * blocklen / 1000;
}


static int aec_resize(struct audiocore_st *st)
{
	int err = 0;
	size_t bytes;
	bool plbproc = false;
	struct aufilt_prm *prm;
	size_t sampc;

	if (!st)
		return EINVAL;

	if (st->est && st->dst && st->est->sampc != st->dst->sampc) {
		if (st->est->started) {
			warning("ac_symphony: sampc differ between enc and "
				"dec.  %lu vs %lu samples.\n",
				st->est->sampc, st->dst->sampc);
			return EINVAL;
		}

		return 0;
	}

	st->est->started = true;

	if (st->est && st->dst &&
			( (st->est->prm.srate != st->dst->prm.srate) ||
			  (st->est->prm.fmt != st->dst->prm.fmt) ||
			  (st->est->prm.ch != st->dst->prm.ch) )) {
		warning("ac_symphony: filter format does not match."
			" (%u/%d/%u) vs (%u/%d/%u).\n",
			st->est->prm.ch, st->est->prm.fmt, st->est->prm.srate,
			st->dst->prm.ch, st->dst->prm.fmt, st->dst->prm.srate);
		return EINVAL;
	}

	if (st->est) {
		prm = &st->est->prm;
		sampc = st->est->sampc;
	}
	else if (st->dst) {
		prm = &st->dst->prm;
		sampc = st->dst->sampc;
	}
	else {
		return EINVAL;
	}

	/* The function ac_ProcessPulseAudioFrameBuffer expects non-interleaved
	 * mic left-/right- data. But baresip works with interleaved. So we
	 * support only mono. */
	if (prm->ch != 1) {
		warning("ac_symphony: this module only supports one mic "
				"channel\n");
		return EINVAL;
	}

	if (st->samplerate != prm->srate || st->ch != prm->ch ||
			st->sampc != sampc) {
		audiocore_st_destructor(st);

		st->samplerate = prm->srate;
		st->ch = prm->ch;
		st->sampc = sampc;
		st->nblock = audiocore_nblock(prm->srate);
		bytes = sizeof(int16_t) * st->sampc;

		err  = aubuf_alloc(&st->decinp, bytes, 2 * bytes);
		conf_get_bool(conf_cur(), "ac_symphony_playback_proc",
				&plbproc);
		if (plbproc)
			err  = aubuf_alloc(&st->decout, bytes, 2 * bytes);
		err  = aubuf_alloc(&st->encinp, bytes, 2 * bytes);
		err  = aubuf_alloc(&st->encout, bytes, 2 * bytes);

		st->encbuf = (int16_t *) mem_zalloc(bytes, 0);
		st->decbuf = (int16_t *) mem_zalloc(bytes, 0);

		st->ac = ac_InitConfigure(st->samplerate, st->ch, 0, 0);
		if (!st->ac) {
			warning("ac_symphony: could not create audiocore with "
				"samplerate=%d, ch=%d\n", st->samplerate,
				st->ch);
			err = ENOMEM;
			return err;
		}

		info("ac_symphony: created audiocore with samplerate=%d, "
				"ch=%d\n", st->samplerate, st->ch);
	}

	if (err)
		return err;

	return 0;
}


static int encode_update(struct aufilt_enc_st **stp, void **ctx,
			 const struct aufilt *af, struct aufilt_prm *prm,
			 const struct audio *au)
{
	struct enc_st *st;
	(void)au;

	if (!stp || !ctx || !af || !prm)
		return EINVAL;

	if (*stp)
		return 0;

	if (!audiocoreState)
		return EINVAL;

	info("ac_symphony: encode_update\n");

	st = (struct enc_st*) mem_zalloc(sizeof(*st), enc_destructor);
	if (!st)
		return ENOMEM;

	st->prm = *prm;
	*stp = (struct aufilt_enc_st *)st;
	audiocoreState->est = st;
	return 0;
}


static int decode_update(struct aufilt_dec_st **stp, void **ctx,
			 const struct aufilt *af, struct aufilt_prm *prm,
			 const struct audio *au)
{
	struct dec_st *st;
	(void)au;

	if (!stp || !ctx || !af || !prm)
		return EINVAL;

	if (*stp)
		return 0;

	if (!audiocoreState)
		return EINVAL;

	info("ac_symphony: decode_update\n");

	st = (struct dec_st *) mem_zalloc(sizeof(*st), dec_destructor);
	if (!st)
		return ENOMEM;

	st->prm = *prm;
	*stp = (struct aufilt_dec_st *)st;
	audiocoreState->dst = st;
	return 0;
}


static int encode(struct aufilt_enc_st *st, struct auframe *af)
{
	struct enc_st *est = (struct enc_st *) st;
	struct audiocore_st *state = audiocoreState;
	size_t bytes = auframe_size(af);
	size_t acbytes;
	int err = 0;

	if (!af || !af->sampc)
		return EINVAL;

	est->sampc = af->sampc;
	err = aec_resize(state);
	if (err)
		return err;

	if (!state->ac)
		return 0;

	if (!state->decinp || !state->encinp || !state->encout)
		return ENOMEM;

	acbytes = state->nblock * sizeof(int16_t) * state->ch;
	if (!acbytes || !state->decbuf || !state->encbuf) {
		warning("ac_symphony: buffers not initialized\n");
		return EINVAL;
	}

	/* Write 20ms. */
	aubuf_write(state->encinp, (uint8_t *) af->sampv, bytes);

	while (aubuf_cur_size(state->encinp) >= acbytes) {
		/* Read 16ms. */
		aubuf_read(state->decinp, (uint8_t *) state->decbuf, acbytes);
		aubuf_read(state->encinp, (uint8_t *) state->encbuf, acbytes);

		ac_ProcessPulseAudioFrameBuffer(state->ac,
				state->decbuf, state->encbuf, state->encbuf,
				state->decbuf,
				state->nblock, state->ch);

		/* Write 16ms.*/
		if (state->decout)
			aubuf_write(state->decout, (uint8_t *) state->decbuf,
					acbytes);

		aubuf_write(state->encout, (uint8_t *) state->encbuf, acbytes);
	}

	/* Read 20ms. */
	aubuf_read(state->encout, (uint8_t *) af->sampv, bytes);

	return 0;
}


static int decode(struct aufilt_dec_st *st, struct auframe *af)
{
	struct dec_st *dst = (struct dec_st *) st;
	struct audiocore_st *state = audiocoreState;
	size_t bytes;
	int err = 0;

	if (!af || !af->sampc)
		return EINVAL;

	bytes = af->sampc * sizeof(int16_t);
	dst->sampc = af->sampc;
	err = aec_resize(state);
	if (err)
		return err;

	if (!state->ac)
		return EINVAL;

	if (!state->decinp)
		return EINVAL;

	if (bytes) {
		aubuf_write(state->decinp, (uint8_t *) af->sampv, bytes);
		if (state->decout)
			aubuf_read(state->decout, (uint8_t *) af->sampv,
					bytes);
	}

	return 0;
}


static struct aufilt audiocore_aec = {
	LE_INIT, "audiocore_aec", encode_update, encode, decode_update, decode
};


static int module_init(void)
{
	struct aufilt_prm prm;

	info("ac_symphony: module_init\n");

	/* Startup setup for audiocore. Will be re-allocated later if
	 * necessary. */
	prm.srate = 16000;
	conf_get_u32(conf_cur(), "ac_symphony_srate", &prm.srate);
	prm.ch = 1;

	if (!audiocoreState)
		audiocoreState = (struct audiocore_st *)
			mem_zalloc(sizeof(*audiocoreState),
					audiocore_st_destructor);

	/* Set default values for audiocore */
	if (!audiocoreState) {
		warning("ac_symphony: could not allocate audiocore state\n");
		return ENOMEM;
	}

	/* register audio filter */
	aufilt_register(baresip_aufiltl(), &audiocore_aec);

	return 0;
}


static int module_close(void)
{
	info("ac_symphony: module_close\n");
	aufilt_unregister(&audiocore_aec);
	audiocoreState = (struct audiocore_st *) mem_deref(audiocoreState);

	return 0;
}


extern "C" EXPORT_SYM const struct mod_export DECL_EXPORTS(ac_symphony) = {
	"ac_symphony",
	"filter",
	module_init,
	module_close
};
