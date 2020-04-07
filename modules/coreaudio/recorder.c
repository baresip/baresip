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
	struct ausrc_prm prm;
	uint32_t sampsz;
	int fmt;
	ausrc_read_h *rh;
	void *arg;
};


static void ausrc_destructor(void *arg)
{
	struct ausrc_st *st = arg;
	uint32_t i;

	pthread_mutex_lock(&st->mutex);
	st->rh = NULL;
	pthread_mutex_unlock(&st->mutex);

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
	struct auframe af;
	ausrc_read_h *rh;
	void *arg;
	(void)inStartTime;
	(void)inNumPackets;
	(void)inPacketDesc;

	pthread_mutex_lock(&st->mutex);
	rh  = st->rh;
	arg = st->arg;
	pthread_mutex_unlock(&st->mutex);

	if (!rh)
		return;

	auframe_init(&af, st->fmt, inQB->mAudioData,
		     inQB->mAudioDataByteSize/st->sampsz);

	af.timestamp = AUDIO_TIMEBASE*inStartTime->mSampleTime / st->prm.srate;

	rh(&af, arg);

	AudioQueueEnqueueBuffer(inQ, inQB, 0, NULL);
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

	if (!stp || !as || !prm)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), ausrc_destructor);
	if (!st)
		return ENOMEM;

	st->as  = as;
	st->rh  = rh;
	st->arg = arg;

	st->sampsz = (uint32_t)aufmt_sample_size(prm->fmt);
	if (!st->sampsz) {
		err = ENOTSUP;
		goto out;
	}

	sampc = prm->srate * prm->ch * prm->ptime / 1000;
	bytc  = sampc * st->sampsz;
	st->fmt = prm->fmt;
	st->prm = *prm;

	err = pthread_mutex_init(&st->mutex, NULL);
	if (err)
		goto out;

	fmt.mSampleRate       = (Float64)prm->srate;
	fmt.mFormatID         = kAudioFormatLinearPCM;
	fmt.mFormatFlags      = coreaudio_aufmt_to_formatflags(prm->fmt) |
		                kAudioFormatFlagIsPacked;
#ifdef __BIG_ENDIAN__
	fmt.mFormatFlags     |= kAudioFormatFlagIsBigEndian;
#endif

	fmt.mFramesPerPacket  = 1;
	fmt.mBytesPerFrame    = prm->ch * st->sampsz;
	fmt.mBytesPerPacket   = prm->ch * st->sampsz;
	fmt.mChannelsPerFrame = prm->ch;
	fmt.mBitsPerChannel   = 8 * st->sampsz;

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

		err = coreaudio_enum_devices(device, NULL, &uid, true);
		if (err)
			goto out;

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


int coreaudio_recorder_init(struct ausrc *as)
{
	if (!as)
		return EINVAL;

	list_init(&as->dev_list);

	return coreaudio_enum_devices (NULL, &as->dev_list, NULL, true);
}
