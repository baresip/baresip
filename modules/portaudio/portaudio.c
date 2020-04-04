/**
 * @file portaudio.c  Portaudio sound driver
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <stdlib.h>
#include <string.h>
#include <portaudio.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>


/**
 * @defgroup portaudio portaudio
 *
 * Portaudio audio driver
 *
 * (portaudio v19 is required)
 *
 *
 * References:
 *
 *    http://www.portaudio.com/
 */


struct ausrc_st {
	const struct ausrc *as;      /* inheritance */
	PaStream *stream_rd;
	ausrc_read_h *rh;
	void *arg;
	volatile bool ready;
	unsigned ch;
	enum aufmt fmt;
};

struct auplay_st {
	const struct auplay *ap;      /* inheritance */
	PaStream *stream_wr;
	auplay_write_h *wh;
	void *arg;
	volatile bool ready;
	unsigned ch;
};


static struct ausrc *ausrc;
static struct auplay *auplay;


/*
 * This routine will be called by the PortAudio engine when audio is needed.
 * It may called at interrupt level on some machines so don't do anything
 * that could mess up the system like calling malloc() or free().
 */
static int read_callback(const void *inputBuffer, void *outputBuffer,
			 unsigned long frameCount,
			 const PaStreamCallbackTimeInfo *timeInfo,
			 PaStreamCallbackFlags statusFlags, void *userData)
{
	struct ausrc_st *st = userData;
	struct auframe af;
	size_t sampc;

	(void)outputBuffer;
	(void)timeInfo;
	(void)statusFlags;

	if (!st->ready)
		return paAbort;

	sampc = frameCount * st->ch;

	af.fmt   = st->fmt;
	af.sampv = (void *)inputBuffer;
	af.sampc = sampc;
	af.timestamp = Pa_GetStreamTime(st->stream_rd) * AUDIO_TIMEBASE;

	st->rh(&af, st->arg);

	return paContinue;
}


static int write_callback(const void *inputBuffer, void *outputBuffer,
			  unsigned long frameCount,
			  const PaStreamCallbackTimeInfo *timeInfo,
			  PaStreamCallbackFlags statusFlags, void *userData)
{
	struct auplay_st *st = userData;
	size_t sampc;

	(void)inputBuffer;
	(void)timeInfo;
	(void)statusFlags;

	if (!st->ready)
		return paAbort;

	sampc = frameCount * st->ch;

	st->wh(outputBuffer, sampc, st->arg);

	return paContinue;
}


static PaSampleFormat aufmt_to_pasampleformat(enum aufmt fmt)
{
	switch (fmt) {

	case AUFMT_S16LE: return paInt16;
	case AUFMT_FLOAT: return paFloat32;
	default: return 0;
	}
}


static int read_stream_open(struct ausrc_st *st, const struct ausrc_prm *prm,
			    uint32_t dev)
{
	PaStreamParameters prm_in;
	PaError err;
	unsigned long frames_per_buffer = prm->srate * prm->ptime / 1000;

	memset(&prm_in, 0, sizeof(prm_in));
	prm_in.device           = dev;
	prm_in.channelCount     = prm->ch;
	prm_in.sampleFormat     = aufmt_to_pasampleformat(prm->fmt);
	prm_in.suggestedLatency = 0.100;

	st->stream_rd = NULL;
	err = Pa_OpenStream(&st->stream_rd, &prm_in, NULL, prm->srate,
			    frames_per_buffer, paNoFlag, read_callback, st);
	if (paNoError != err) {
		warning("portaudio: read: Pa_OpenStream: %s\n",
			Pa_GetErrorText(err));
		return EINVAL;
	}

	err = Pa_StartStream(st->stream_rd);
	if (paNoError != err) {
		warning("portaudio: read: Pa_StartStream: %s\n",
			Pa_GetErrorText(err));
		return EINVAL;
	}

	return 0;
}


static int write_stream_open(struct auplay_st *st,
			     const struct auplay_prm *prm, uint32_t dev)
{
	PaStreamParameters prm_out;
	PaError err;
	unsigned long frames_per_buffer = prm->srate * prm->ptime / 1000;

	memset(&prm_out, 0, sizeof(prm_out));
	prm_out.device           = dev;
	prm_out.channelCount     = prm->ch;
	prm_out.sampleFormat     = aufmt_to_pasampleformat(prm->fmt);
	prm_out.suggestedLatency = 0.100;

	st->stream_wr = NULL;
	err = Pa_OpenStream(&st->stream_wr, NULL, &prm_out, prm->srate,
			    frames_per_buffer, paNoFlag, write_callback, st);
	if (paNoError != err) {
		warning("portaudio: write: Pa_OpenStream: %s\n",
			Pa_GetErrorText(err));
		return EINVAL;
	}

	err = Pa_StartStream(st->stream_wr);
	if (paNoError != err) {
		warning("portaudio: write: Pa_StartStream: %s\n",
			Pa_GetErrorText(err));
		return EINVAL;
	}

	return 0;
}


static void ausrc_destructor(void *arg)
{
	struct ausrc_st *st = arg;

	st->ready = false;

	if (st->stream_rd) {
		Pa_AbortStream(st->stream_rd);
		Pa_CloseStream(st->stream_rd);
	}
}


static void auplay_destructor(void *arg)
{
	struct auplay_st *st = arg;

	st->ready = false;

	if (st->stream_wr) {
		Pa_AbortStream(st->stream_wr);
		Pa_CloseStream(st->stream_wr);
	}
}


static int src_alloc(struct ausrc_st **stp, const struct ausrc *as,
		     struct media_ctx **ctx,
		     struct ausrc_prm *prm, const char *device,
		     ausrc_read_h *rh, ausrc_error_h *errh, void *arg)
{
	struct ausrc_st *st;
	PaDeviceIndex dev_index;
	int err;

	(void)ctx;
	(void)device;
	(void)errh;

	if (!stp || !as || !prm)
		return EINVAL;

	if (str_isset(device))
		dev_index = atoi(device);
	else
		dev_index = Pa_GetDefaultInputDevice();

	st = mem_zalloc(sizeof(*st), ausrc_destructor);
	if (!st)
		return ENOMEM;

	st->as  = as;
	st->rh  = rh;
	st->arg = arg;
	st->ch  = prm->ch;
	st->fmt = prm->fmt;

	st->ready = true;

	err = read_stream_open(st, prm, dev_index);
	if (err)
		goto out;

 out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}


static int play_alloc(struct auplay_st **stp, const struct auplay *ap,
		      struct auplay_prm *prm, const char *device,
		      auplay_write_h *wh, void *arg)
{
	struct auplay_st *st;
	PaDeviceIndex dev_index;
	int err;

	(void)device;

	if (!stp || !ap || !prm)
		return EINVAL;

	if (str_isset(device))
		dev_index = atoi(device);
	else
		dev_index = Pa_GetDefaultOutputDevice();

	st = mem_zalloc(sizeof(*st), auplay_destructor);
	if (!st)
		return ENOMEM;

	st->ap  = ap;
	st->wh  = wh;
	st->arg = arg;
	st->ch  = prm->ch;

	st->ready = true;

	err = write_stream_open(st, prm, dev_index);
	if (err)
		goto out;

 out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}


static int pa_init(void)
{
	PaError paerr;
	int i, n, err = 0;

	paerr = Pa_Initialize();
	if (paNoError != paerr) {
		warning("portaudio: init: %s\n", Pa_GetErrorText(paerr));
		return ENODEV;
	}

	n = Pa_GetDeviceCount();

	info("portaudio: device count is %d\n", n);

	for (i=0; i<n; i++) {
		const PaDeviceInfo *devinfo;

		devinfo = Pa_GetDeviceInfo(i);

		debug("portaudio: device %d: %s\n", i, devinfo->name);
		(void)devinfo;
	}

	if (paNoDevice != Pa_GetDefaultInputDevice())
		err |= ausrc_register(&ausrc, baresip_ausrcl(),
				      "portaudio", src_alloc);

	if (paNoDevice != Pa_GetDefaultOutputDevice())
		err |= auplay_register(&auplay, baresip_auplayl(),
				       "portaudio", play_alloc);

	return err;
}


static int pa_close(void)
{
	ausrc  = mem_deref(ausrc);
	auplay = mem_deref(auplay);

	Pa_Terminate();

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(portaudio) = {
	"portaudio",
	"sound",
	pa_init,
	pa_close
};
