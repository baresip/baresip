/**
 * @file winwave/play.c Windows sound driver -- playback
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <re.h>
#include <rem.h>
#include <windows.h>
#include <mmsystem.h>
#include <baresip.h>
#include "winwave.h"


#define WRITE_BUFFERS  4
#define INC_WPOS(a) ((a) = (((a) + 1) % WRITE_BUFFERS))


struct auplay_st {
	const struct auplay *ap;      /* inheritance */
	struct dspbuf bufs[WRITE_BUFFERS];
	int pos;
	HWAVEOUT waveout;
	volatile bool rdy;
	size_t inuse;
	auplay_write_h *wh;
	void *arg;
};


static void auplay_destructor(void *arg)
{
	struct auplay_st *st = arg;
	int i;

	st->wh = NULL;

	/* Mark the device for closing, and wait for all the
	 * buffers to be returned by the driver
	 */
	st->rdy = false;
	while (st->inuse > 0)
		Sleep(50);

	waveOutReset(st->waveout);

	for (i = 0; i < WRITE_BUFFERS; i++) {
		waveOutUnprepareHeader(st->waveout, &st->bufs[i].wh,
				       sizeof(WAVEHDR));
		mem_deref(st->bufs[i].mb);
	}

	waveOutClose(st->waveout);
}


static int dsp_write(struct auplay_st *st)
{
	MMRESULT res;
	WAVEHDR *wh;
	struct mbuf *mb;

	if (!st->rdy)
		return EINVAL;

	wh = &st->bufs[st->pos].wh;
	if (wh->dwFlags & WHDR_PREPARED) {
		return EINVAL;
	}
	mb = st->bufs[st->pos].mb;
	wh->lpData = (LPSTR)mb->buf;

	if (st->wh) {
		st->wh((void *)mb->buf, mb->size/2, st->arg);
	}

	wh->dwBufferLength = mb->size;
	wh->dwFlags = 0;
	wh->dwUser = (DWORD_PTR) mb;

	waveOutPrepareHeader(st->waveout, wh, sizeof(*wh));

	INC_WPOS(st->pos);

	res = waveOutWrite(st->waveout, wh, sizeof(*wh));
	if (res != MMSYSERR_NOERROR)
		warning("winwave: dsp_write: waveOutWrite: failed: %08x\n",
			res);
	else
		st->inuse++;

	return 0;
}


static void CALLBACK waveOutCallback(HWAVEOUT hwo,
				     UINT uMsg,
				     DWORD_PTR dwInstance,
				     DWORD_PTR dwParam1,
				     DWORD_PTR dwParam2)
{
	struct auplay_st *st = (struct auplay_st *)dwInstance;
	WAVEHDR *wh = (WAVEHDR *)dwParam1;

	(void)hwo;
	(void)dwParam2;

	switch (uMsg) {

	case WOM_OPEN:
		st->rdy = true;
		break;

	case WOM_DONE:
		/*LOCK();*/
		waveOutUnprepareHeader(st->waveout, wh, sizeof(*wh));
		/*UNLOCK();*/
		st->inuse--;
		dsp_write(st);
		break;

	case WOM_CLOSE:
		st->rdy = false;
		break;

	default:
		break;
	}
}


static unsigned int find_dev(const char *name)
{
	WAVEOUTCAPS wic;
	unsigned int i, nInDevices = waveOutGetNumDevs();

	if (!str_isset(name))
		return WAVE_MAPPER;

	for (i=0; i<nInDevices; i++) {
		if (waveOutGetDevCaps(i, &wic,
				      sizeof(WAVEOUTCAPS))==MMSYSERR_NOERROR) {

			if (0 == str_cmp(name, wic.szPname)) {
				return i;
			}
		}
	}

	return WAVE_MAPPER;
}


static int write_stream_open(struct auplay_st *st,
			     const struct auplay_prm *prm,
			     unsigned int dev)
{
	WAVEFORMATEX wfmt;
	MMRESULT res;
	uint32_t sampc;
	int i;

	/* Open an audio I/O stream. */
	st->waveout = NULL;
	st->pos = 0;
	st->rdy = false;

	sampc = prm->srate * prm->ch * prm->ptime / 1000;

	for (i = 0; i < WRITE_BUFFERS; i++) {
		memset(&st->bufs[i].wh, 0, sizeof(WAVEHDR));
		st->bufs[i].mb = mbuf_alloc(2 * sampc);
	}

	wfmt.wFormatTag      = WAVE_FORMAT_PCM;
	wfmt.nChannels       = prm->ch;
	wfmt.nSamplesPerSec  = prm->srate;
	wfmt.wBitsPerSample  = 16;
	wfmt.nBlockAlign     = (prm->ch * wfmt.wBitsPerSample) / 8;
	wfmt.nAvgBytesPerSec = wfmt.nSamplesPerSec * wfmt.nBlockAlign;
	wfmt.cbSize          = 0;

	res = waveOutOpen(&st->waveout, dev, &wfmt,
			  (DWORD_PTR) waveOutCallback,
			  (DWORD_PTR) st,
			  CALLBACK_FUNCTION | WAVE_FORMAT_DIRECT);
	if (res != MMSYSERR_NOERROR) {
		warning("winwave: waveOutOpen: failed %d\n", res);
		return EINVAL;
	}

	return 0;
}


int winwave_play_alloc(struct auplay_st **stp, const struct auplay *ap,
		       struct auplay_prm *prm, const char *device,
		       auplay_write_h *wh, void *arg)
{
	struct auplay_st *st;
	int i, err;

	if (!stp || !ap || !prm)
		return EINVAL;

	if (prm->fmt != AUFMT_S16LE) {
		warning("winwave: playback: unsupported sample format (%s)\n",
			aufmt_name(prm->fmt));
		return ENOTSUP;
	}

	st = mem_zalloc(sizeof(*st), auplay_destructor);
	if (!st)
		return ENOMEM;

	st->ap  = ap;
	st->wh  = wh;
	st->arg = arg;

	err = write_stream_open(st, prm, find_dev(device));
	if (err)
		goto out;

	/* The write runs at 100ms intervals
	 * prepare enough buffers to suite its needs
	 */
	for (i = 0; i < 5; i++)
		dsp_write(st);

 out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}
