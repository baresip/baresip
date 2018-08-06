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
	const struct ausrc *as;      /* inheritance */
	AudioQueueRef queue;
	AudioQueueBufferRef buf[BUFC];
	pthread_mutex_t mutex;
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

	pthread_mutex_destroy(&st->mutex);
}


static void record_handler(void *userData, AudioQueueRef inQ,
			   AudioQueueBufferRef inQB,
			   const AudioTimeStamp *inStartTime,
			   UInt32 inNumPackets,
			   const AudioStreamPacketDescription *inPacketDesc)
{
	struct ausrc_st *st = userData;
	unsigned int ptime;
	ausrc_read_h *rh;
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

	rh(inQB->mAudioData, inQB->mAudioDataByteSize/2, arg);

	AudioQueueEnqueueBuffer(inQ, inQB, 0, NULL);

	/* Force a sleep here, coreaudio's timing is too fast */
#if !TARGET_OS_IPHONE
#define ENCODE_TIME 1000
	usleep((ptime * 1000) - ENCODE_TIME);
#endif
}


int coreaudio_recorder_alloc(struct ausrc_st **stp, const struct ausrc *as,
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
	(void)errh;

	if (!stp || !as || !prm || prm->fmt != AUFMT_S16LE)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), ausrc_destructor);
	if (!st)
		return ENOMEM;

	st->ptime = prm->ptime;
	st->as  = as;
	st->rh  = rh;
	st->arg = arg;

	sampc = prm->srate * prm->ch * prm->ptime / 1000;
	bytc  = sampc * 2;

	err = pthread_mutex_init(&st->mutex, NULL);
	if (err)
		goto out;

	err = audio_session_enable();
	if (err)
		goto out;

	fmt.mSampleRate       = (Float64)prm->srate;
	fmt.mFormatID         = kAudioFormatLinearPCM;
	fmt.mFormatFlags      = kLinearPCMFormatFlagIsSignedInteger |
		                kAudioFormatFlagIsPacked;
#ifdef __BIG_ENDIAN__
	fmt.mFormatFlags     |= kAudioFormatFlagIsBigEndian;
#endif

	fmt.mFramesPerPacket  = 1;
	fmt.mBytesPerFrame    = prm->ch * 2;
	fmt.mBytesPerPacket   = prm->ch * 2;
	fmt.mChannelsPerFrame = prm->ch;
	fmt.mBitsPerChannel   = 16;

	status = AudioQueueNewInput(&fmt, record_handler, st, NULL,
				     kCFRunLoopCommonModes, 0, &st->queue);
	if (status) {
		warning("coreaudio: AudioQueueNewInput error: %i\n", status);
		err = ENODEV;
		goto out;
	}

	if (str_isset(device) && 0 != str_casecmp(device, "default")) {

		CFStringRef uid;

		info("coreaudio: recorder: using device '%s'\n", device);

		uid = coreaudio_get_device_uid(device);
		if (!uid) {
			warning("coreaudio: recorder: device not found:"
				" '%s'\n", device);
			err = ENODEV;
			goto out;
		}

		status = AudioQueueSetProperty(st->queue,
				       kAudioQueueProperty_CurrentDevice,
				       &uid,
				       sizeof(uid));
		CFRelease(uid);
		if (status) {
			warning("coreaudio: recorder: failed to"
				" set current device (%i)\n", status);
			err = ENODEV;
			goto out;
		}
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
