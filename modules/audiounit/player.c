/**
 * @file audiounit/player.c  AudioUnit output player
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <AudioUnit/AudioUnit.h>
#include <AudioToolbox/AudioToolbox.h>
#include <pthread.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "audiounit.h"


struct auplay_st {
	const struct auplay *ap;      /* inheritance */
	struct audiosess_st *sess;
	AudioUnit au;
	pthread_mutex_t mutex;
	auplay_write_h *wh;
	void *arg;
	uint32_t sampsz;
};


static void auplay_destructor(void *arg)
{
	struct auplay_st *st = arg;

	pthread_mutex_lock(&st->mutex);
	st->wh = NULL;
	pthread_mutex_unlock(&st->mutex);

	AudioOutputUnitStop(st->au);
	AudioUnitUninitialize(st->au);
	AudioComponentInstanceDispose(st->au);

	mem_deref(st->sess);

	pthread_mutex_destroy(&st->mutex);
}


static OSStatus output_callback(void *inRefCon,
				AudioUnitRenderActionFlags *ioActionFlags,
				const AudioTimeStamp *inTimeStamp,
				UInt32 inBusNumber,
				UInt32 inNumberFrames,
				AudioBufferList *ioData)
{
	struct auplay_st *st = inRefCon;
	auplay_write_h *wh;
	void *arg;
	uint32_t i;

	(void)ioActionFlags;
	(void)inTimeStamp;
	(void)inBusNumber;
	(void)inNumberFrames;

	pthread_mutex_lock(&st->mutex);
	wh  = st->wh;
	arg = st->arg;
	pthread_mutex_unlock(&st->mutex);

	if (!wh)
		return 0;

	for (i = 0; i < ioData->mNumberBuffers; ++i) {

		AudioBuffer *ab = &ioData->mBuffers[i];

		wh(ab->mData, ab->mDataByteSize/st->sampsz, arg);
	}

	return 0;
}


static void interrupt_handler(bool interrupted, void *arg)
{
	struct auplay_st *st = arg;

	if (interrupted)
		AudioOutputUnitStop(st->au);
	else
		AudioOutputUnitStart(st->au);
}


static uint32_t aufmt_to_formatflags(enum aufmt fmt)
{
	switch (fmt) {

	case AUFMT_S16LE:  return kLinearPCMFormatFlagIsSignedInteger;
	case AUFMT_FLOAT:  return kLinearPCMFormatFlagIsFloat;
	default: return 0;
	}
}


int audiounit_player_alloc(struct auplay_st **stp, const struct auplay *ap,
			   struct auplay_prm *prm, const char *device,
			   auplay_write_h *wh, void *arg)
{
	AudioStreamBasicDescription fmt;
	AudioUnitElement outputBus = 0;
	AURenderCallbackStruct cb;
	struct auplay_st *st;
	UInt32 enable = 1;
	OSStatus ret = 0;
	Float64 hw_srate = 0.0;
	UInt32 hw_size = sizeof(hw_srate);
	int err;

	(void)device;

	st = mem_zalloc(sizeof(*st), auplay_destructor);
	if (!st)
		return ENOMEM;

	st->ap  = ap;
	st->wh  = wh;
	st->arg = arg;

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
				   kAudioUnitScope_Output, outputBus,
				   &enable, sizeof(enable));
	if (ret) {
		warning("audiounit: EnableIO failed (%d)\n", ret);
		goto out;
	}

	st->sampsz = (uint32_t)aufmt_sample_size(prm->fmt);

	fmt.mSampleRate       = prm->srate;
	fmt.mFormatID         = kAudioFormatLinearPCM;
#if TARGET_OS_IPHONE
	fmt.mFormatFlags      = aufmt_to_formatflags(prm->fmt)
		| kAudioFormatFlagsNativeEndian
		| kAudioFormatFlagIsPacked;
#else
	fmt.mFormatFlags      = aufmt_to_formatflags(prm->fmt)
		| kAudioFormatFlagIsPacked;
#endif
	fmt.mBitsPerChannel   = 8 * st->sampsz;
	fmt.mChannelsPerFrame = prm->ch;
	fmt.mBytesPerFrame    = st->sampsz * prm->ch;
	fmt.mFramesPerPacket  = 1;
	fmt.mBytesPerPacket   = st->sampsz * prm->ch;

	ret = AudioUnitInitialize(st->au);
	if (ret)
		goto out;

	ret = AudioUnitSetProperty(st->au, kAudioUnitProperty_StreamFormat,
				   kAudioUnitScope_Input, outputBus,
				   &fmt, sizeof(fmt));
	if (ret)
		goto out;

	cb.inputProc = output_callback;
	cb.inputProcRefCon = st;
	ret = AudioUnitSetProperty(st->au,
				   kAudioUnitProperty_SetRenderCallback,
				   kAudioUnitScope_Input, outputBus,
				   &cb, sizeof(cb));
	if (ret)
		goto out;

	ret = AudioOutputUnitStart(st->au);
	if (ret)
		goto out;

	ret = AudioUnitGetProperty(st->au,
				   kAudioUnitProperty_SampleRate,
				   kAudioUnitScope_Output,
				   0,
				   &hw_srate,
				   &hw_size);
	if (ret)
		goto out;

	debug("audiounit: player hardware sample rate is now at %f Hz\n",
	      hw_srate);

 out:
	if (ret) {
		warning("audiounit: player failed: %d (%c%c%c%c)\n", ret,
			ret>>24, ret>>16, ret>>8, ret);
		err = ENODEV;
	}

	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}
