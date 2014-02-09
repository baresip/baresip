/**
 * @file coreaudio/recorder.c  Apple Coreaudio sound driver - recorder
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <AudioToolbox/AudioQueue.h>
#include <unistd.h>
#include <pthread.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "coreaudio.h"


#define BUFC 3


struct ausrc_st {
	struct ausrc *as;      /* inheritance */
	AudioQueueRef queue;
	AudioQueueBufferRef buf[BUFC];
	pthread_mutex_t mutex;
	struct mbuf *mb;
	ausrc_read_h *rh;
	void *arg;
	unsigned int ptime;
};


static void ausrc_destructor(void *arg)
{
	struct ausrc_st *st = arg;
	uint32_t i;

	pthread_mutex_lock(&st->mutex);
	st->rh = NULL;
	pthread_mutex_unlock(&st->mutex);

	audio_session_disable();

	if (st->queue) {
		AudioQueuePause(st->queue);
		AudioQueueStop(st->queue, true);

		for (i=0; i<ARRAY_SIZE(st->buf); i++)
			if (st->buf[i])
				AudioQueueFreeBuffer(st->queue, st->buf[i]);

		AudioQueueDispose(st->queue, true);
	}

	mem_deref(st->mb);
	mem_deref(st->as);

	pthread_mutex_destroy(&st->mutex);
}


static void record_handler(void *userData, AudioQueueRef inQ,
			   AudioQueueBufferRef inQB,
			   const AudioTimeStamp *inStartTime,
			   UInt32 inNumPackets,
			   const AudioStreamPacketDescription *inPacketDesc)
{
	struct ausrc_st *st = userData;
	struct mbuf *mb = st->mb;
	unsigned int ptime;
	ausrc_read_h *rh;
	size_t sz, sp;
	void *arg;
	(void)inStartTime;
	(void)inNumPackets;
	(void)inPacketDesc;

	pthread_mutex_lock(&st->mutex);
	ptime = st->ptime;
	rh  = st->rh;
	arg = st->arg;
	pthread_mutex_unlock(&st->mutex);

	if (!rh)
		return;

	sz = inQB->mAudioDataByteSize;
	sp = mbuf_get_space(mb);

	if (sz >= sp) {
		mbuf_write_mem(mb, inQB->mAudioData, sp);
		rh(mb->buf, (uint32_t)mb->size, arg);
		mb->pos = 0;
		mbuf_write_mem(mb, (uint8_t *)inQB->mAudioData + sp, sz - sp);
	}
	else {
		mbuf_write_mem(mb, inQB->mAudioData, sz);
	}

	AudioQueueEnqueueBuffer(inQ, inQB, 0, NULL);

	/* Force a sleep here, coreaudio's timing is too fast */
#if !TARGET_OS_IPHONE
#define ENCODE_TIME 1000
	usleep((ptime * 1000) - ENCODE_TIME);
#endif
}


int coreaudio_recorder_alloc(struct ausrc_st **stp, struct ausrc *as,
			     struct media_ctx **ctx,
			     struct ausrc_prm *prm, const char *device,
			     ausrc_read_h *rh, ausrc_error_h *errh, void *arg)
{
	AudioStreamBasicDescription fmt;
	struct ausrc_st *st;
	uint32_t sampc, bytc, i;
	OSStatus status;
	int err;

	(void)ctx;
	(void)device;
	(void)errh;

	if (!stp || !as || !prm)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), ausrc_destructor);
	if (!st)
		return ENOMEM;

	st->ptime = prm->ptime;
	st->as  = mem_ref(as);
	st->rh  = rh;
	st->arg = arg;

	sampc = prm->srate * prm->ch * prm->ptime / 1000;
	bytc  = sampc * bytesps(prm->fmt);

	st->mb = mbuf_alloc(bytc);
	if (!st->mb) {
		err = ENOMEM;
		goto out;
	}

	err = pthread_mutex_init(&st->mutex, NULL);
	if (err)
		goto out;

	err = audio_session_enable();
	if (err)
		goto out;

	fmt.mSampleRate       = (Float64)prm->srate;
	fmt.mFormatID         = audio_fmt(prm->fmt);
	fmt.mFormatFlags      = kLinearPCMFormatFlagIsSignedInteger |
		                kAudioFormatFlagIsPacked;
#ifdef __BIG_ENDIAN__
	fmt.mFormatFlags     |= kAudioFormatFlagIsBigEndian;
#endif

	fmt.mFramesPerPacket  = 1;
	fmt.mBytesPerFrame    = prm->ch * bytesps(prm->fmt);
	fmt.mBytesPerPacket   = prm->ch * bytesps(prm->fmt);
	fmt.mChannelsPerFrame = prm->ch;
	fmt.mBitsPerChannel   = 8*bytesps(prm->fmt);

	status = AudioQueueNewInput(&fmt, record_handler, st, NULL,
				     kCFRunLoopCommonModes, 0, &st->queue);
	if (status) {
		warning("coreaudio: AudioQueueNewInput error: %i\n", status);
		err = ENODEV;
		goto out;
	}

	for (i=0; i<ARRAY_SIZE(st->buf); i++)  {

		status = AudioQueueAllocateBuffer(st->queue, bytc,
						  &st->buf[i]);
		if (status)  {
			err = ENOMEM;
			goto out;
		}

		AudioQueueEnqueueBuffer(st->queue, st->buf[i], 0, NULL);
	}

	status = AudioQueueStart(st->queue, NULL);
	if (status)  {
		warning("coreaudio: AudioQueueStart error %i\n", status);
		err = ENODEV;
		goto out;
	}

 out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}
