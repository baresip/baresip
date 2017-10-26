/**
 * @file winwave/src.c Windows sound driver -- source
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <re.h>
#include <rem.h>
#include <windows.h>
#include <mmsystem.h>
#include <baresip.h>
#include "winwave.h"


#define READ_BUFFERS   4
#define INC_RPOS(a) ((a) = (((a) + 1) % READ_BUFFERS))


struct ausrc_st {
	const struct ausrc *as;      /* inheritance */
	struct dspbuf bufs[READ_BUFFERS];
	int pos;
	HWAVEIN wavein;
	volatile bool rdy;
	size_t inuse;
	ausrc_read_h *rh;
	void *arg;
};


static void ausrc_destructor(void *arg)
{
	struct ausrc_st *st = arg;
	int i;

	st->rh = NULL;

	waveInStop(st->wavein);
	waveInReset(st->wavein);

	for (i = 0; i < READ_BUFFERS; i++) {
		waveInUnprepareHeader(st->wavein, &st->bufs[i].wh,
				      sizeof(WAVEHDR));
		mem_deref(st->bufs[i].mb);
	}

	waveInClose(st->wavein);
}


static int add_wave_in(struct ausrc_st *st)
{
	struct dspbuf *db = &st->bufs[st->pos];
	WAVEHDR *wh = &db->wh;
	MMRESULT res;

	wh->lpData          = (LPSTR)db->mb->buf;
	wh->dwBufferLength  = db->mb->size;
	wh->dwBytesRecorded = 0;
	wh->dwFlags         = 0;
	wh->dwUser          = (DWORD_PTR)db->mb;

	waveInPrepareHeader(st->wavein, wh, sizeof(*wh));
	res = waveInAddBuffer(st->wavein, wh, sizeof(*wh));
	if (res != MMSYSERR_NOERROR) {
		warning("winwave: add_wave_in: waveInAddBuffer fail: %08x\n",
			res);
		return ENOMEM;
	}

	INC_RPOS(st->pos);

	st->inuse++;

	return 0;
}


static void CALLBACK waveInCallback(HWAVEOUT hwo,
				    UINT uMsg,
				    DWORD_PTR dwInstance,
				    DWORD_PTR dwParam1,
				    DWORD_PTR dwParam2)
{
	struct ausrc_st *st = (struct ausrc_st *)dwInstance;
	WAVEHDR *wh = (WAVEHDR *)dwParam1;

	(void)hwo;
	(void)dwParam2;

	if (!st->rh)
		return;

	switch (uMsg) {

	case WIM_CLOSE:
		st->rdy = false;
		break;

	case WIM_OPEN:
		st->rdy = true;
		break;

	case WIM_DATA:
		if (st->inuse < (READ_BUFFERS-1))
			add_wave_in(st);

		st->rh((void *)wh->lpData, wh->dwBytesRecorded/2, st->arg);

		waveInUnprepareHeader(st->wavein, wh, sizeof(*wh));
		st->inuse--;
		break;

	default:
		break;
	}
}


static unsigned int find_dev(const char *name)
{
	WAVEINCAPS wic;
	unsigned int i, nInDevices = waveInGetNumDevs();

	if (!str_isset(name))
		return WAVE_MAPPER;

	for (i=0; i<nInDevices; i++) {
		if (waveInGetDevCaps(i, &wic,
				     sizeof(WAVEINCAPS))==MMSYSERR_NOERROR) {

			if (0 == str_casecmp(name, wic.szPname)) {
				return i;
			}
		}
	}

	return WAVE_MAPPER;
}


static int read_stream_open(struct ausrc_st *st, const struct ausrc_prm *prm,
			    unsigned int dev)
{
	WAVEFORMATEX wfmt;
	MMRESULT res;
	uint32_t sampc;
	int i, err = 0;

	/* Open an audio INPUT stream. */
	st->wavein = NULL;
	st->pos = 0;
	st->rdy = false;

	sampc = prm->srate * prm->ch * prm->ptime / 1000;

	for (i = 0; i < READ_BUFFERS; i++) {
		memset(&st->bufs[i].wh, 0, sizeof(WAVEHDR));
		st->bufs[i].mb = mbuf_alloc(2 * sampc);
		if (!st->bufs[i].mb)
			return ENOMEM;
	}

	wfmt.wFormatTag      = WAVE_FORMAT_PCM;
	wfmt.nChannels       = prm->ch;
	wfmt.nSamplesPerSec  = prm->srate;
	wfmt.wBitsPerSample  = 16;
	wfmt.nBlockAlign     = (prm->ch * wfmt.wBitsPerSample) / 8;
	wfmt.nAvgBytesPerSec = wfmt.nSamplesPerSec * wfmt.nBlockAlign;
	wfmt.cbSize          = 0;

	res = waveInOpen(&st->wavein, dev, &wfmt,
			  (DWORD_PTR) waveInCallback,
			  (DWORD_PTR) st,
			  CALLBACK_FUNCTION | WAVE_FORMAT_DIRECT);
	if (res != MMSYSERR_NOERROR) {
		warning("winwave: waveInOpen: failed %d\n", err);
		return EINVAL;
	}

	/* Prepare enough IN buffers to suite at least 50ms of data */
	for (i = 0; i < READ_BUFFERS; i++)
		err |= add_wave_in(st);

	waveInStart(st->wavein);

	return err;
}


int winwave_src_alloc(struct ausrc_st **stp, const struct ausrc *as,
		      struct media_ctx **ctx,
		      struct ausrc_prm *prm, const char *device,
		      ausrc_read_h *rh, ausrc_error_h *errh, void *arg)
{
	struct ausrc_st *st;
	int err;

	(void)ctx;
	(void)errh;

	if (!stp || !as || !prm)
		return EINVAL;

	if (prm->fmt != AUFMT_S16LE) {
		warning("winwave: source: unsupported sample format (%s)\n",
			aufmt_name(prm->fmt));
		return ENOTSUP;
	}

	st = mem_zalloc(sizeof(*st), ausrc_destructor);
	if (!st)
		return ENOMEM;

	st->as  = as;
	st->rh  = rh;
	st->arg = arg;

	err = read_stream_open(st, prm, find_dev(device));

	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}
