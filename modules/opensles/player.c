/**
 * @file opensles/player.c  OpenSLES audio driver -- playback
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <SLES/OpenSLES.h>
#include "SLES/OpenSLES_Android.h"
#include "opensles.h"


#define N_PLAY_QUEUE_BUFFERS 2
#define PTIME 10


struct auplay_st {
	const struct auplay *ap;      /* inheritance */
	auplay_write_h *wh;
	void *arg;
	int16_t *sampv[N_PLAY_QUEUE_BUFFERS];
	size_t   sampc;
	uint8_t  bufferId;

	SLObjectItf outputMixObject;
	SLObjectItf bqPlayerObject;
	SLPlayItf bqPlayerPlay;
	SLAndroidSimpleBufferQueueItf BufferQueue;
};


static void auplay_destructor(void *arg)
{
	struct auplay_st *st = arg;

	if (st->bqPlayerObject != NULL)
		(*st->bqPlayerObject)->Destroy(st->bqPlayerObject);

	if (st->outputMixObject != NULL)
		(*st->outputMixObject)->Destroy(st->outputMixObject);

	st->bufferId = 0;
	for (int i=0; i<N_PLAY_QUEUE_BUFFERS; i++) {
		mem_deref(st->sampv[i]);
	}
}


static void bqPlayerCallback(SLAndroidSimpleBufferQueueItf bq, void *context)
{
	struct auplay_st *st = context;

	st->wh(st->sampv[st->bufferId], st->sampc, st->arg);

	(*st->BufferQueue)->Enqueue(bq /*st->BufferQueue*/,
				    st->sampv[st->bufferId], st->sampc * 2);

	st->bufferId = ( st->bufferId + 1 ) % N_PLAY_QUEUE_BUFFERS;
}


static int createOutput(struct auplay_st *st)
{
	const SLInterfaceID ids[1] = {SL_IID_ENVIRONMENTALREVERB};
	const SLboolean req[1] = {SL_BOOLEAN_FALSE};
	SLresult r;

	r = (*engineEngine)->CreateOutputMix(engineEngine,
					    &st->outputMixObject, 1, ids, req);
	if (SL_RESULT_SUCCESS != r)
		return ENODEV;

	r = (*st->outputMixObject)->Realize(st->outputMixObject,
					    SL_BOOLEAN_FALSE);
	if (SL_RESULT_SUCCESS != r)
		return ENODEV;

	return 0;
}


static int createPlayer(struct auplay_st *st, struct auplay_prm *prm)
{
	SLDataLocator_AndroidSimpleBufferQueue loc_bufq = {
		SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 2
	};
	uint32_t ch_mask = prm->ch == 2
		? SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT
		: SL_SPEAKER_FRONT_CENTER;
	SLDataFormat_PCM format_pcm = {SL_DATAFORMAT_PCM, prm->ch,
				       prm->srate * 1000,
				       SL_PCMSAMPLEFORMAT_FIXED_16,
				       SL_PCMSAMPLEFORMAT_FIXED_16,
				       ch_mask,
				       SL_BYTEORDER_LITTLEENDIAN};
	SLDataSource audioSrc = {&loc_bufq, &format_pcm};
	SLDataLocator_OutputMix loc_outmix = {
		SL_DATALOCATOR_OUTPUTMIX, st->outputMixObject
	};
	SLDataSink audioSnk = {&loc_outmix, NULL};
	const SLInterfaceID ids[2] = {SL_IID_BUFFERQUEUE, SL_IID_EFFECTSEND};
	const SLboolean req[2] = {SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE};
	SLresult r;

	r = (*engineEngine)->CreateAudioPlayer(engineEngine,
					       &st->bqPlayerObject,
					       &audioSrc, &audioSnk,
					       ARRAY_SIZE(ids), ids, req);
	if (SL_RESULT_SUCCESS != r) {
		warning("opensles: CreateAudioPlayer error: r = %d\n", r);
		return ENODEV;
	}

	r = (*st->bqPlayerObject)->Realize(st->bqPlayerObject,
					   SL_BOOLEAN_FALSE);
	if (SL_RESULT_SUCCESS != r)
		return ENODEV;

	r = (*st->bqPlayerObject)->GetInterface(st->bqPlayerObject,
						SL_IID_PLAY,
						&st->bqPlayerPlay);
	if (SL_RESULT_SUCCESS != r)
		return ENODEV;

	r = (*st->bqPlayerObject)->GetInterface(st->bqPlayerObject,
						SL_IID_BUFFERQUEUE,
						&st->BufferQueue);
	if (SL_RESULT_SUCCESS != r)
		return ENODEV;

	r = (*st->BufferQueue)->RegisterCallback(st->BufferQueue,
						 bqPlayerCallback, st);
	if (SL_RESULT_SUCCESS != r)
		return ENODEV;

	r = (*st->bqPlayerPlay)->SetPlayState(st->bqPlayerPlay,
					      SL_PLAYSTATE_PLAYING);
	if (SL_RESULT_SUCCESS != r)
		return ENODEV;

	return 0;
}


int opensles_player_alloc(struct auplay_st **stp, const struct auplay *ap,
			  struct auplay_prm *prm, const char *device,
			  auplay_write_h *wh, void *arg)
{
	struct auplay_st *st;
	int err;
	(void)device;

	if (!stp || !ap || !prm || !wh)
		return EINVAL;

	if (prm->fmt != AUFMT_S16LE) {
		warning("opensles: player: unsupported sample format (%s)\n",
			aufmt_name(prm->fmt));
		return ENOTSUP;
	}

	debug("opensles: opening player %uHz, %uchannels\n",
			prm->srate, prm->ch);

	st = mem_zalloc(sizeof(*st), auplay_destructor);
	if (!st)
		return ENOMEM;

	st->ap  = ap;
	st->wh  = wh;
	st->arg = arg;

	st->sampc = prm->srate * prm->ch * PTIME / 1000;

	st->bufferId   = 0;
	for (int i=0; i<N_PLAY_QUEUE_BUFFERS; i++) {
		st->sampv[i] = mem_zalloc(2 * st->sampc, NULL);
		if (!st->sampv[i]) {
			err = ENOMEM;
			goto out;
		}
	}

	err = createOutput(st);
	if (err)
		goto out;

	err = createPlayer(st, prm);
	if (err)
		goto out;

	/* kick-start the buffer callback */
	bqPlayerCallback(st->BufferQueue, st);

 out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}
