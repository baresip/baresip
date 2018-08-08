/**
 * @file coreaudio/player.c  Apple Coreaudio sound driver - player
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <AudioToolbox/AudioQueue.h>
#include <pthread.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "coreaudio.h"


/* This value can be tuned */
#if TARGET_OS_IPHONE
#define BUFC 20
#else
#define BUFC 6
#endif


struct auplay_st {
	const struct auplay *ap;      /* inheritance */
	AudioQueueRef queue;
	AudioQueueBufferRef buf[BUFC];
	pthread_mutex_t mutex;
	auplay_write_h *wh;
	void *arg;
};


static void auplay_destructor(void *arg)
{
	struct auplay_st *st = arg;
	uint32_t i;

	pthread_mutex_lock(&st->mutex);
	st->wh = NULL;
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


static void play_handler(void *userData, AudioQueueRef outQ,
			 AudioQueueBufferRef outQB)
{
	struct auplay_st *st = userData;
	auplay_write_h *wh;
	void *arg;

	pthread_mutex_lock(&st->mutex);
	wh  = st->wh;
	arg = st->arg;
	pthread_mutex_unlock(&st->mutex);

	if (!wh)
		return;

	wh(outQB->mAudioData, outQB->mAudioDataByteSize/2, arg);

	AudioQueueEnqueueBuffer(outQ, outQB, 0, NULL);
}


int coreaudio_player_alloc(struct auplay_st **stp, const struct auplay *ap,
			   struct auplay_prm *prm, const char *device,
			   auplay_write_h *wh, void *arg)
{
	AudioStreamBasicDescription fmt;
	struct auplay_st *st;
	uint32_t sampc, bytc, i;
	OSStatus status;
	int err;

	if (!stp || !ap || !prm || prm->fmt != AUFMT_S16LE)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), auplay_destructor);
	if (!st)
		return ENOMEM;

	st->ap  = ap;
	st->wh  = wh;
	st->arg = arg;

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

	status = AudioQueueNewOutput(&fmt, play_handler, st, NULL,
				     kCFRunLoopCommonModes, 0, &st->queue);
	if (status) {
		warning("coreaudio: AudioQueueNewOutput error: %i\n", status);
		err = ENODEV;
		goto out;
	}

	if (str_isset(device) && 0 != str_casecmp(device, "default")) {

		CFStringRef uid;

		info("coreaudio: player: using device '%s'\n", device);

		uid = coreaudio_get_device_uid(device);
		if (!uid) {
			warning("coreaudio: player: device not found: '%s'\n",
				device);
			err = ENODEV;
			goto out;
		}

		status = AudioQueueSetProperty(st->queue,
				       kAudioQueueProperty_CurrentDevice,
				       &uid,
				       sizeof(uid));
		CFRelease(uid);
		if (status) {
			warning("coreaudio: player: failed to"
				" set current device (%i)\n", status);
			err = ENODEV;
			goto out;
		}
	}

	sampc = prm->srate * prm->ch * prm->ptime / 1000;
	bytc  = sampc * 2;

	for (i=0; i<ARRAY_SIZE(st->buf); i++)  {

		status = AudioQueueAllocateBuffer(st->queue, bytc,
						  &st->buf[i]);
		if (status)  {
			err = ENOMEM;
			goto out;
		}

		st->buf[i]->mAudioDataByteSize = bytc;

		memset(st->buf[i]->mAudioData, 0,
		       st->buf[i]->mAudioDataByteSize);

		(void)AudioQueueEnqueueBuffer(st->queue, st->buf[i], 0, NULL);
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
