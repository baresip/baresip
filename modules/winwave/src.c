/**
 * @file winwave/src.c Windows sound driver -- source
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <re.h>
#include <rem.h>
#include <windows.h>
#include <mmsystem.h>
#include <mmreg.h>
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
	size_t sampsz;
	enum aufmt fmt;
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
	struct auframe af;

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

		af.fmt   = st->fmt;
		af.sampv = (void *)wh->lpData;
		af.sampc = wh->dwBytesRecorded/st->sampsz;
		af.timestamp = tmr_jiffies_usec();

		st->rh(&af, st->arg);

		waveInUnprepareHeader(st->wavein, wh, sizeof(*wh));
		st->inuse--;
		break;

	default:
		break;
	}
}


static int read_stream_open(struct ausrc_st *st, const struct ausrc_prm *prm,
			    unsigned int dev)
{
	WAVEFORMATEX wfmt;
	MMRESULT res;
	uint32_t sampc;
	unsigned format;
	int i, err = 0;

	st->sampsz = aufmt_sample_size(prm->fmt);

	format = winwave_get_format(prm->fmt);
	if (format == WAVE_FORMAT_UNKNOWN) {
		warning("winwave: source: unsupported sample format (%s)\n",
			aufmt_name(prm->fmt));
		return ENOTSUP;
	}

	/* Open an audio INPUT stream. */
	st->wavein = NULL;
	st->pos = 0;
	st->rdy = false;
	st->fmt = prm->fmt;

	sampc = prm->srate * prm->ch * prm->ptime / 1000;

	for (i = 0; i < READ_BUFFERS; i++) {
		memset(&st->bufs[i].wh, 0, sizeof(WAVEHDR));
		st->bufs[i].mb = mbuf_alloc(st->sampsz * sampc);
		if (!st->bufs[i].mb)
			return ENOMEM;
	}

	wfmt.wFormatTag      = format;
	wfmt.nChannels       = prm->ch;
	wfmt.nSamplesPerSec  = prm->srate;
	wfmt.wBitsPerSample  = (WORD)(st->sampsz * 8);
	wfmt.nBlockAlign     = prm->ch * st->sampsz;
	wfmt.nAvgBytesPerSec = wfmt.nSamplesPerSec * wfmt.nBlockAlign;
	wfmt.cbSize          = 0;

	res = waveInOpen(&st->wavein, dev, &wfmt,
			  (DWORD_PTR) waveInCallback,
			  (DWORD_PTR) st,
			  CALLBACK_FUNCTION | WAVE_FORMAT_DIRECT);
	if (res != MMSYSERR_NOERROR) {
		warning("winwave: waveInOpen: failed res=%d\n", res);
		return EINVAL;
	}

	/* Prepare enough IN buffers to suite at least 50ms of data */
	for (i = 0; i < READ_BUFFERS; i++)
		err |= add_wave_in(st);

	waveInStart(st->wavein);

	return err;
}


static int winwave_get_dev_name(unsigned int i, char name[32])
{
	WAVEINCAPS wic;
	int err = 0;

	if (waveInGetDevCaps(i, &wic,
			     sizeof(WAVEINCAPS)) == MMSYSERR_NOERROR) {
		str_ncpy(name, wic.szPname, 32);
	}
	else {
		err = ENODEV;
	}

	return err;
}


static unsigned int winwave_get_num_devs(void)
{
	return waveInGetNumDevs();
}


static int find_dev(const char *name, unsigned int *dev)
{
	return winwave_enum_devices(name, NULL, dev, winwave_get_num_devs,
				    winwave_get_dev_name);
}


int winwave_src_alloc(struct ausrc_st **stp, const struct ausrc *as,
		      struct media_ctx **ctx,
		      struct ausrc_prm *prm, const char *device,
		      ausrc_read_h *rh, ausrc_error_h *errh, void *arg)
{
	struct ausrc_st *st;
	int err;
	unsigned int dev;

	(void)ctx;
	(void)errh;

	if (!stp || !as || !prm)
		return EINVAL;

	err = find_dev(device, &dev);
	if (err)
		return err;

	st = mem_zalloc(sizeof(*st), ausrc_destructor);
	if (!st)
		return ENOMEM;

	st->as  = as;
	st->rh  = rh;
	st->arg = arg;

	err |= read_stream_open(st, prm, dev);

	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}


static int set_available_devices(struct list *dev_list)
{
	return winwave_enum_devices(NULL, dev_list, NULL,
				    winwave_get_num_devs,
				    winwave_get_dev_name);
}


int winwave_src_init(struct ausrc *as)
{
	if (!as)
		return EINVAL;

	list_init(&as->dev_list);

	return set_available_devices(&as->dev_list);
}
