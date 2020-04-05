/**
 * @file opensles/recorder.c  OpenSLES audio driver -- recording
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <string.h>
#include <SLES/OpenSLES.h>
#include "SLES/OpenSLES_Android.h"
#include "opensles.h"


#define N_REC_QUEUE_BUFFERS 2
#define PTIME 10


struct ausrc_st {
	const struct ausrc *as;      /* inheritance */

	int16_t *sampv[N_REC_QUEUE_BUFFERS];
	size_t   sampc;
	uint8_t  bufferId;
	ausrc_read_h *rh;
	void *arg;

	SLObjectItf recObject;
	SLRecordItf recRecord;
	SLAndroidSimpleBufferQueueItf recBufferQueue;
};


static void ausrc_destructor(void *arg)
{
	struct ausrc_st *st = arg;

	if (st->recObject != NULL) {
		SLuint32 state;

		if (SL_RESULT_SUCCESS ==
		    (*st->recObject)->GetState(st->recObject, &state) &&
		    SL_OBJECT_STATE_UNREALIZED != state) {
			(*st->recObject)->Destroy(st->recObject);
		}
	}

	st->bufferId = 0;
	for (int i=0; i<N_REC_QUEUE_BUFFERS; i++) {
		mem_deref(st->sampv[i]);
	}
}


static void bqRecorderCallback(SLAndroidSimpleBufferQueueItf bq, void *context)
{
	struct ausrc_st *st = context;
	struct auframe af = {
		.fmt   = AUFMT_S16LE,
		.sampv = st->sampv[st->bufferId],
		.sampc = st->sampc,
		.timestamp = tmr_jiffies_usec()
	};
	(void)bq;

	st->rh(&af, st->arg);

	st->bufferId = ( st->bufferId + 1 ) % N_REC_QUEUE_BUFFERS;

	memset(st->sampv[st->bufferId], 0, st->sampc * 2);

	(*st->recBufferQueue)->Enqueue(st->recBufferQueue,
				       st->sampv[st->bufferId],
				       st->sampc * 2);
}


static int createAudioRecorder(struct ausrc_st *st, struct ausrc_prm *prm)
{
	SLDataLocator_IODevice loc_dev = {SL_DATALOCATOR_IODEVICE,
					  SL_IODEVICE_AUDIOINPUT,
					  SL_DEFAULTDEVICEID_AUDIOINPUT,
					  NULL};
	SLDataSource audioSrc = {&loc_dev, NULL};

	SLDataLocator_AndroidSimpleBufferQueue loc_bq = {
		SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 2
	};
	int speakers = prm->ch > 1
		? SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT
		: SL_SPEAKER_FRONT_CENTER;
	SLDataFormat_PCM format_pcm = {SL_DATAFORMAT_PCM, prm->ch,
				       prm->srate * 1000,
				       SL_PCMSAMPLEFORMAT_FIXED_16,
				       SL_PCMSAMPLEFORMAT_FIXED_16,
				       speakers,
				       SL_BYTEORDER_LITTLEENDIAN};
	SLDataSink audioSnk = {&loc_bq, &format_pcm};
	const SLInterfaceID id[1] = {SL_IID_ANDROIDSIMPLEBUFFERQUEUE};
	const SLboolean req[1] = {SL_BOOLEAN_TRUE};
	SLresult r;

	r = (*engineEngine)->CreateAudioRecorder(engineEngine,
						 &st->recObject,
						 &audioSrc,
						 &audioSnk, 1, id, req);
	if (SL_RESULT_SUCCESS != r) {
		warning("opensles: CreateAudioRecorder failed: r = %d\n", r);
		return ENODEV;
	}

	r = (*st->recObject)->Realize(st->recObject, SL_BOOLEAN_FALSE);
	if (SL_RESULT_SUCCESS != r)
		return ENODEV;

	r = (*st->recObject)->GetInterface(st->recObject, SL_IID_RECORD,
					   &st->recRecord);
	if (SL_RESULT_SUCCESS != r)
		return ENODEV;

	r = (*st->recObject)->GetInterface(st->recObject,
					   SL_IID_ANDROIDSIMPLEBUFFERQUEUE,
					   &st->recBufferQueue);
	if (SL_RESULT_SUCCESS != r)
		return ENODEV;

	r = (*st->recBufferQueue)->RegisterCallback(st->recBufferQueue,
						    bqRecorderCallback,
						    st);
	if (SL_RESULT_SUCCESS != r)
		return ENODEV;

	return 0;
}


static int startRecording(struct ausrc_st *st)
{
	SLresult r;

	(*st->recRecord)->SetRecordState(st->recRecord,
					 SL_RECORDSTATE_STOPPED);
	(*st->recBufferQueue)->Clear(st->recBufferQueue);

	st->bufferId = 0;
	r = (*st->recBufferQueue)->Enqueue(st->recBufferQueue,
					   st->sampv[st->bufferId],
					   st->sampc * 2);
	if (SL_RESULT_SUCCESS != r)
		return ENODEV;

	r = (*st->recRecord)->SetRecordState(st->recRecord,
					     SL_RECORDSTATE_RECORDING);
	if (SL_RESULT_SUCCESS != r)
		return ENODEV;

	return 0;
}


int opensles_recorder_alloc(struct ausrc_st **stp, const struct ausrc *as,
			    struct media_ctx **ctx,
			    struct ausrc_prm *prm, const char *device,
			    ausrc_read_h *rh, ausrc_error_h *errh, void *arg)
{
	struct ausrc_st *st;
	int err;
	(void)ctx;
	(void)device;
	(void)errh;

	if (!stp || !as || !prm || !rh)
		return EINVAL;

	if (prm->fmt != AUFMT_S16LE) {
		warning("opensles: record: unsupported sample format (%s)\n",
			aufmt_name(prm->fmt));
		return ENOTSUP;
	}

	debug("opensles: opening recorder %uHz, %uchannels\n",
			prm->srate, prm->ch);

	st = mem_zalloc(sizeof(*st), ausrc_destructor);
	if (!st)
		return ENOMEM;

	st->as  = as;
	st->rh  = rh;
	st->arg = arg;

	st->sampc = prm->srate * prm->ch * PTIME / 1000;
	st->bufferId   = 0;
	for (int i=0; i<N_REC_QUEUE_BUFFERS; i++) {
		st->sampv[i] = mem_zalloc(2 * st->sampc, NULL);
		if (!st->sampv[i]) {
			err = ENOMEM;
			goto out;
		}
	}

	err = createAudioRecorder(st, prm);
	if (err)
		goto out;

	err = startRecording(st);
	if (err) {
		warning("opensles: failed to start recorder\n");
		goto out;
	}

 out:

	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}
