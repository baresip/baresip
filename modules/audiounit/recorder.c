/**
 * @file audiounit/recorder.c  AudioUnit input recorder
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <AudioUnit/AudioUnit.h>
#include <AudioToolbox/AudioToolbox.h>
#include <pthread.h>
#include <re.h>
#include <baresip.h>
#include "audiounit.h"


struct ausrc_st {
	struct ausrc *as;      /* inheritance */
	struct audiosess_st *sess;
	AudioUnit au;
	pthread_mutex_t mutex;
	int ch;
	ausrc_read_h *rh;
	void *arg;
};


static void ausrc_destructor(void *arg)
{
	struct ausrc_st *st = arg;

	pthread_mutex_lock(&st->mutex);
	st->rh = NULL;
	pthread_mutex_unlock(&st->mutex);

	AudioOutputUnitStop(st->au);
	AudioUnitUninitialize(st->au);
	AudioComponentInstanceDispose(st->au);

	mem_deref(st->sess);
	mem_deref(st->as);

	pthread_mutex_destroy(&st->mutex);
}


static OSStatus input_callback(void *inRefCon,
			       AudioUnitRenderActionFlags *ioActionFlags,
			       const AudioTimeStamp *inTimeStamp,
			       UInt32 inBusNumber,
			       UInt32 inNumberFrames,
			       AudioBufferList *ioData)
{
	struct ausrc_st *st = inRefCon;
	AudioBufferList abl;
	OSStatus ret;
	ausrc_read_h *rh;
	void *arg;

	(void)ioData;

	pthread_mutex_lock(&st->mutex);
	rh  = st->rh;
	arg = st->arg;
	pthread_mutex_unlock(&st->mutex);

	if (!rh)
		return 0;

	abl.mNumberBuffers = 1;
	abl.mBuffers[0].mNumberChannels = st->ch;
	abl.mBuffers[0].mData = NULL;
	abl.mBuffers[0].mDataByteSize = inNumberFrames * 2;

	ret = AudioUnitRender(st->au,
			      ioActionFlags,
			      inTimeStamp,
			      inBusNumber,
			      inNumberFrames,
			      &abl);
	if (ret)
		return ret;

	rh(abl.mBuffers[0].mData, abl.mBuffers[0].mDataByteSize/2, arg);

	return 0;
}


static void interrupt_handler(bool interrupted, void *arg)
{
	struct ausrc_st *st = arg;

	if (interrupted)
		AudioOutputUnitStop(st->au);
	else
		AudioOutputUnitStart(st->au);
}


int audiounit_recorder_alloc(struct ausrc_st **stp, struct ausrc *as,
			     struct media_ctx **ctx,
			     struct ausrc_prm *prm, const char *device,
			     ausrc_read_h *rh, ausrc_error_h *errh, void *arg)
{
	AudioStreamBasicDescription fmt;
	AudioUnitElement inputBus = 1;
	AURenderCallbackStruct cb;
	struct ausrc_st *st;
	UInt32 enable = 1;
	OSStatus ret = 0;
	int err;

	(void)ctx;
	(void)device;
	(void)errh;

	st = mem_zalloc(sizeof(*st), ausrc_destructor);
	if (!st)
		return ENOMEM;

	st->as  = mem_ref(as);
	st->rh  = rh;
	st->arg = arg;
	st->ch  = prm->ch;

	err = pthread_mutex_init(&st->mutex, NULL);
	if (err)
		goto out;

	err = audiosess_alloc(&st->sess, interrupt_handler, st);
	if (err)
		goto out;

	ret = AudioComponentInstanceNew(output_comp, &st->au);
	if (ret)
		goto out;

	ret = AudioUnitSetProperty(st->au, kAudioOutputUnitProperty_EnableIO,
				   kAudioUnitScope_Input, inputBus,
				   &enable, sizeof(enable));
	if (ret)
		goto out;

	fmt.mSampleRate       = prm->srate;
	fmt.mFormatID         = kAudioFormatLinearPCM;
#if TARGET_OS_IPHONE
	fmt.mFormatFlags      = kAudioFormatFlagsCanonical;
#else
	fmt.mFormatFlags      = kLinearPCMFormatFlagIsSignedInteger
		| kLinearPCMFormatFlagIsPacked;
#endif
	fmt.mBitsPerChannel   = 16;
	fmt.mChannelsPerFrame = prm->ch;
	fmt.mBytesPerFrame    = 2 * prm->ch;
	fmt.mFramesPerPacket  = 1;
	fmt.mBytesPerPacket   = 2 * prm->ch;
	fmt.mReserved         = 0;

	ret = AudioUnitSetProperty(st->au, kAudioUnitProperty_StreamFormat,
				   kAudioUnitScope_Output, inputBus,
				   &fmt, sizeof(fmt));
	if (ret)
		goto out;

	/* NOTE: done after desc */
	ret = AudioUnitInitialize(st->au);
	if (ret)
		goto out;

	cb.inputProc = input_callback;
	cb.inputProcRefCon = st;
	ret = AudioUnitSetProperty(st->au,
				   kAudioOutputUnitProperty_SetInputCallback,
				   kAudioUnitScope_Global, inputBus,
				   &cb, sizeof(cb));
	if (ret)
		goto out;

	ret = AudioOutputUnitStart(st->au);
	if (ret)
		goto out;

 out:
	if (ret) {
		warning("audiounit: record failed: %d (%c%c%c%c)\n", ret,
			ret>>24, ret>>16, ret>>8, ret);
		err = ENODEV;
	}

	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}
