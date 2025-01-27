/**
 * @file wasapi/src.c Windows Audio Session API (WASAPI)
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


struct ausrc_st {
	thrd_t thread;
	RE_ATOMIC bool run;
	struct ausrc_prm prm;
	ausrc_read_h *rh;
	struct pl *device;
	void *sampv;
	size_t sampc;
	void *arg;
};


static void ausrc_destructor(void *arg)
{
	struct ausrc_st *st = arg;
	/* Wait for termination of other thread */
	if (re_atomic_rlx(&st->run)) {
		re_atomic_rlx_set(&st->run, false);
		thrd_join(st->thread, NULL);
	}

	mem_deref(st->sampv);
	mem_deref(st->device);
}


static int src_thread(void *arg)
{
	struct ausrc_st *st = arg;
	bool started = false;
	HRESULT hr;
	IMMDevice *capturer		= NULL;
	IMMDeviceEnumerator *enumerator = NULL;
	IAudioClient *client		= NULL;
	IAudioCaptureClient *service	= NULL;
	WAVEFORMATEX *format		= NULL;
	LPWSTR device			= NULL;
	uint32_t num_frames_buffer	= 0;
	uint32_t packet_sz = 0;
	DWORD flags;
	uint32_t num_frames = st->prm.srate * st->prm.ptime / 1000;
	int err = 0;

	struct auframe af;
	auframe_init(&af, st->prm.fmt, st->sampv, st->sampc, st->prm.srate,
		     st->prm.ch);

	hr = CoInitializeEx(NULL, COINIT_MULTITHREADED |
					  COINIT_DISABLE_OLE1DDE |
					  COINIT_SPEED_OVER_MEMORY);
	CHECK_HR(hr, "wasapi/src: CoInitializeEx failed");

	hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL,
			      &IID_IMMDeviceEnumerator, (void **)&enumerator);
	CHECK_HR(hr, "wasapi/src: CoCreateInstance failed");

	if (pl_strcasecmp(st->device, "default") == 0) {
		hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(
			enumerator, eCapture, eCommunications, &capturer);
		CHECK_HR(hr, "wasapi/src: GetDefaultAudioEndpoint failed");
	}
	else {
		err = wasapi_wc_from_utf8(&device, st->device);
		if (err)
			goto out;
		hr = IMMDeviceEnumerator_GetDevice(enumerator, device,
						   &capturer);
		CHECK_HR(hr, "wasapi/src: GetDevice failed");
	}

	hr = IMMDevice_Activate(capturer, &IID_IAudioClient, CLSCTX_ALL,
				NULL, (void **)&client);
	CHECK_HR(hr, "wasapi/src: IMMDevice_Activate failed");

	hr = IAudioClient_GetMixFormat(client, &format);
	CHECK_HR(hr, "wasapi/src: GetMixFormat failed");

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
	CHECK_HR(hr, "wasapi/src: IAudioClient_Initialize failed");

	hr = IAudioClient_GetService(client, &IID_IAudioCaptureClient,
				     (void **)&service);
	CHECK_HR(hr, "wasapi/src: IAudioClient_GetService failed");

	hr = IAudioClient_GetBufferSize(client, &num_frames_buffer);
	CHECK_HR(hr, "wasapi/src: IAudioClient_GetBufferSize failed");

	hr = IAudioClient_Start(client);
	CHECK_HR(hr, "wasapi/src: IAudioClient_Start failed");

	started = true;

	while (re_atomic_rlx(&st->run)) {
		hr = IAudioCaptureClient_GetNextPacketSize(service,
						    &packet_sz);
		CHECK_HR(hr, "wasapi/src: GetNextPacketSize failed");

		if (packet_sz == 0) {
			sys_msleep(5);
			continue;
		}

		hr = IAudioCaptureClient_GetBuffer(service, (BYTE **)&af.sampv,
						   &num_frames, &flags, NULL,
						   NULL);
		CHECK_HR(hr, "wasapi/src: GetBuffer failed");

		af.timestamp = tmr_jiffies_usec();
		af.sampc = num_frames * format->nChannels;

		st->rh(&af, st->arg);

		hr = IAudioCaptureClient_ReleaseBuffer(service, num_frames);
		CHECK_HR(hr, "wasapi/src: ReleaseBuffer failed");
	}

out:
	if (started)
		IAudioClient_Stop(client);
	if (service)
		IAudioCaptureClient_Release(service);
	if (client)
		IAudioClient_Release(client);
	if (capturer)
		IMMDevice_Release(capturer);
	if (enumerator)
		IMMDeviceEnumerator_Release(enumerator);

	CoTaskMemFree(format);
	CoUninitialize();
	mem_deref(device);

	return err;
}


int wasapi_src_alloc(struct ausrc_st **stp, const struct ausrc *as,
		      struct ausrc_prm *prm, const char *device,
		      ausrc_read_h *rh, ausrc_error_h *errh, void *arg)
{
	struct ausrc_st *st;
	int err;
	(void)errh;

	if (!stp || !as || !prm)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), ausrc_destructor);
	if (!st)
		return ENOMEM;

	st->rh  = rh;
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
	err = thread_create_name(&st->thread, "wasapi_src", src_thread, st);
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
