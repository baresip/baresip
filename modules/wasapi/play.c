/**
 * @file wasapi/play.c Windows Audio Session API (WASAPI)
 *
 * Copyright (C) 2024 Sebastian Reimers
 * Copyright (C) 2024 AGFEO GmbH & Co. KG
 */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define CINTERFACE
#define COBJMACROS
#define CONST_VTABLE
#include <initguid.h>
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audiosessiontypes.h>
#include <audiopolicy.h>
#include <functiondiscoverykeys_devpkey.h>

#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <re_atomic.h>

#include "wasapi.h"


struct auplay_st {
	thrd_t thread;
	RE_ATOMIC bool run;
	struct auplay_prm prm;
	auplay_write_h *wh;
	struct pl *device;
	void *sampv;
	size_t sampc;
	void *arg;
};


static void auplay_destructor(void *arg)
{
	struct auplay_st *st = arg;
	/* Wait for termination of other thread */
	if (re_atomic_rlx(&st->run)) {
		re_atomic_rlx_set(&st->run, false);
		thrd_join(st->thread, NULL);
	}

	mem_deref(st->sampv);
	mem_deref(st->device);
}

static int play_thread(void *arg)
{
	struct auplay_st *st = arg;
	bool started = false;
	HRESULT hr;
	IMMDevice *renderer		= NULL;
	IMMDeviceEnumerator *enumerator = NULL;
	IAudioClient *client		= NULL;
	IAudioRenderClient *service	= NULL;
	WAVEFORMATEX *format		= NULL;
	LPWSTR device			= NULL;
	uint32_t num_frames_buffer  = 0;
	uint32_t num_frames_padding = 0;
	void *sampv		    = NULL;
	int err = 0;

	uint32_t num_frames = st->prm.srate * st->prm.ptime / 1000;

	struct auframe af;
	auframe_init(&af, st->prm.fmt, st->sampv, st->sampc, st->prm.srate,
		     st->prm.ch);

	hr = CoInitializeEx(NULL, COINIT_MULTITHREADED |
					  COINIT_DISABLE_OLE1DDE |
					  COINIT_SPEED_OVER_MEMORY);
	CHECK_HR(hr, "wasapi/play: CoInitializeEx failed");

	hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL,
			      &IID_IMMDeviceEnumerator, (void **)&enumerator);
	CHECK_HR(hr, "wasapi/play: CoCreateInstance failed");

	if (pl_strcasecmp(st->device, "default") == 0) {
		hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(
			enumerator, eRender, eCommunications, &renderer);
		CHECK_HR(hr, "wasapi/play: GetDefaultAudioEndpoint failed");
	}
	else {
		err = wasapi_wc_from_utf8(&device, st->device);
		if (err)
			goto out;
		hr = IMMDeviceEnumerator_GetDevice(enumerator, device,
						   &renderer);
		CHECK_HR(hr, "wasapi/play: GetDevice failed");
	}

	hr = IMMDevice_Activate(renderer, &IID_IAudioClient, CLSCTX_ALL,
				NULL, (void **)&client);
	CHECK_HR(hr, "wasapi/play: IMMDevice_Activate failed");

	hr = IAudioClient_GetMixFormat(client, &format);
	CHECK_HR(hr, "wasapi/play: GetMixFormat failed");

	format->wFormatTag = WAVE_FORMAT_PCM;
	format->nChannels = st->prm.ch;
	format->nSamplesPerSec = st->prm.srate;
	format->wBitsPerSample = (WORD)aufmt_sample_size(st->prm.fmt) * 8;
	format->nBlockAlign = (format->wBitsPerSample / 8) * format->nChannels;
	format->nAvgBytesPerSec = format->nSamplesPerSec * format->nBlockAlign;
	format->cbSize = 0;

	hr = IAudioClient_Initialize(
		client, AUDCLNT_SHAREMODE_SHARED,
		AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
			AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
		(st->prm.ptime * REF_PER_MS * 2), 0, format, NULL);
	CHECK_HR(hr, "wasapi/play: IAudioClient_Initialize failed");

	hr = IAudioClient_GetService(client, &IID_IAudioRenderClient,
				     (void **)&service);
	CHECK_HR(hr, "wasapi/play: IAudioClient_GetService failed");

	hr = IAudioClient_GetBufferSize(client, &num_frames_buffer);
	CHECK_HR(hr, "wasapi/play: IAudioClient_GetBufferSize failed");

	hr = IAudioClient_Start(client);
	CHECK_HR(hr, "wasapi/play: IAudioClient_Start failed");

	started = true;

	while (re_atomic_rlx(&st->run)) {
		hr = IAudioClient_GetCurrentPadding(client,
						    &num_frames_padding);
		CHECK_HR(hr, "wasapi/play: GetCurrentPadding failed");

		if ((num_frames_buffer - num_frames_padding) < num_frames) {
			sys_msleep(5);
			continue;
		}

		st->wh(&af, st->arg);

		hr = IAudioRenderClient_GetBuffer(service, num_frames,
						  (BYTE **)&sampv);
		CHECK_HR(hr, "wasapi/play: GetBuffer failed");

		memcpy(sampv, af.sampv, format->nBlockAlign * num_frames);

		hr = IAudioRenderClient_ReleaseBuffer(service, num_frames, 0);
		CHECK_HR(hr, "wasapi/play: ReleaseBuffer failed");
	}


out:
	if (started)
		IAudioClient_Stop(client);
	if (service)
		IAudioRenderClient_Release(service);
	if (client)
		IAudioClient_Release(client);
	if (enumerator)
		IMMDeviceEnumerator_Release(enumerator);

	CoTaskMemFree(format);
	CoUninitialize();
	mem_deref(device);

	return err;
}


int wasapi_play_alloc(struct auplay_st **stp, const struct auplay *ap,
		      struct auplay_prm *prm, const char *device,
		      auplay_write_h *wh, void *arg)
{
	struct auplay_st *st;
	int err = 0;

	if (!stp || !ap || !prm)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), auplay_destructor);
	if (!st)
		return ENOMEM;

	st->wh	= wh;
	st->arg = arg;
	st->prm = *prm;

	st->device = pl_alloc_str(device);
	if (!st->device) {
		err = ENOMEM;
		goto out;
	}

	st->sampc = prm->srate * prm->ch * prm->ptime / 1000;
	st->sampv = mem_alloc(aufmt_sample_size(prm->fmt) * st->sampc, NULL);
	if (!st->sampv) {
		err = ENOMEM;
		goto out;
	}

	re_atomic_rlx_set(&st->run, true);
	err = thread_create_name(&st->thread, "wasapi_play", play_thread, st);
	if (err) {
		re_atomic_rlx_set(&st->run, false);
		goto out;
	}

out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}
