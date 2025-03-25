/**
 * @file portaudio.c  Portaudio sound driver
 *
 * Copyright (C) 2010 Alfred E. Heggestad
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
	PaStream *stream_rd;
	ausrc_read_h *rh;
	void *arg;
	volatile bool ready;
	struct ausrc_prm prm;
};

struct auplay_st {
	PaStream *stream_wr;
	auplay_write_h *wh;
	void *arg;
	volatile bool ready;
	struct auplay_prm prm;
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

	sampc = frameCount * st->prm.ch;

	auframe_init(&af, st->prm.fmt, (void *)inputBuffer, sampc,
		     st->prm.srate, st->prm.ch);
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
	struct auframe af;
	size_t sampc;

	(void)inputBuffer;
	(void)timeInfo;
	(void)statusFlags;

	if (!st->ready)
		return paAbort;

	sampc = frameCount * st->prm.ch;

	auframe_init(&af, st->prm.fmt, outputBuffer, sampc, st->prm.srate,
		     st->prm.ch);

	st->wh(&af, st->arg);

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

static int find_device(const struct list *dev_list, const char *device)
{
	struct mediadev *dev;
	char *endp = NULL;
	int dev_index;

	dev = str_isset(device) && 0 != str_casecmp(device, "default")
		? mediadev_find(dev_list, device)
		: mediadev_get_default(dev_list);
	if (dev)
		return dev->device_index;

	/*
	 * Accept a numeric index as well for backwards compatibility.
	 * This will only be supported for the audio_source and audio_player
	 * commands and is not supported by any other audio backend.
	 */
	dev_index = (int)strtol(device, &endp, 10);
	return *endp == '\0' ? dev_index : -1;
}

static int src_alloc(struct ausrc_st **stp, const struct ausrc *as,
		     struct ausrc_prm *prm, const char *device,
		     ausrc_read_h *rh, ausrc_error_h *errh, void *arg)
{
	struct ausrc_st *st;
	int dev_index;
	int err;

	(void)device;
	(void)errh;

	if (!stp || !as || !prm)
		return EINVAL;

	dev_index = find_device(&ausrc->dev_list, device);
	if (dev_index < 0)
		return ENODEV;

	st = mem_zalloc(sizeof(*st), ausrc_destructor);
	if (!st)
		return ENOMEM;

	st->rh  = rh;
	st->arg = arg;
	st->prm = *prm;

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
	int dev_index;
	int err;

	(void)device;

	if (!stp || !ap || !prm)
		return EINVAL;

	dev_index = find_device(&auplay->dev_list, device);
	if (dev_index < 0)
		return ENODEV;

	st = mem_zalloc(sizeof(*st), auplay_destructor);
	if (!st)
		return ENOMEM;

	st->wh  = wh;
	st->arg = arg;
	st->prm  = *prm;

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
	int err = 0;

	if (log_level_get() == LEVEL_DEBUG) {
		paerr = Pa_Initialize();
	}
	else {
		fs_stdio_hide();
		paerr = Pa_Initialize();
		fs_stdio_restore();
	}
	if (paNoError != paerr) {
		warning("portaudio: init: %s\n", Pa_GetErrorText(paerr));
		return ENODEV;
	}

	if (paNoDevice != Pa_GetDefaultInputDevice())
		err |= ausrc_register(&ausrc, baresip_ausrcl(), "portaudio",
				      src_alloc);

	if (paNoDevice != Pa_GetDefaultOutputDevice())
		err |= auplay_register(&auplay, baresip_auplayl(), "portaudio",
				       play_alloc);

	if (err)
		return err;

	int n = Pa_GetDeviceCount();

	info("portaudio: device count is %d\n", n);

	for (int i = 0; i < n; i++) {
		struct mediadev *dev;
		char devname[128];

		const PaDeviceInfo *devinfo = Pa_GetDeviceInfo(i);
		if (!devinfo)
			continue;

		const PaHostApiInfo *apiinfo =
			Pa_GetHostApiInfo(devinfo->hostApi);
		if (!apiinfo)
			continue;

		re_snprintf(devname, sizeof(devname), "%s: %s", apiinfo->name,
			    devinfo->name);

		debug("portaudio: device %d: %s\n", i, devname);

		if (devinfo->maxInputChannels > 0) {
			err = mediadev_add(&ausrc->dev_list, devname);
			if (err) {
				warning("portaudio: mediadev err %m\n", err);
				return err;
			}

			dev = mediadev_find(&ausrc->dev_list, devname);
			if (!dev)
				continue;

			dev->host_index	  = devinfo->hostApi;
			dev->device_index = i;
			dev->src.is_default =
				(i == Pa_GetDefaultInputDevice());
			dev->src.channels = devinfo->maxInputChannels;
		}

		if (devinfo->maxOutputChannels > 0) {
			err = mediadev_add(&auplay->dev_list, devname);
			if (err) {
				warning("portaudio: mediadev err %m\n", err);
				return err;
			}

			dev = mediadev_find(&auplay->dev_list, devname);
			if (!dev)
				continue;

			dev->host_index	  = devinfo->hostApi;
			dev->device_index = i;
			dev->play.is_default =
				(i == Pa_GetDefaultOutputDevice());
			dev->play.channels = devinfo->maxOutputChannels;
		}
	}

	return 0;
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
