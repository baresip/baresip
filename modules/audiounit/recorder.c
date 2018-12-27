/**
 * @file audiounit/recorder.c  AudioUnit input recorder
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <AudioUnit/AudioUnit.h>
#include <AudioToolbox/AudioToolbox.h>
#include <TargetConditionals.h>
#include <pthread.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "audiounit.h"


struct ausrc_st {
	const struct ausrc *as;      /* inheritance */
	struct audiosess_st *sess;
	AudioUnit au;
	pthread_mutex_t mutex;
	int ch;
	uint32_t sampsz;
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
	abl.mBuffers[0].mDataByteSize = inNumberFrames * st->sampsz;

	ret = AudioUnitRender(st->au,
			      ioActionFlags,
			      inTimeStamp,
			      inBusNumber,
			      inNumberFrames,
			      &abl);
	if (ret) {
		debug("audiounit: record: AudioUnitRender error (%d)\n", ret);
		return ret;
	}

	rh(abl.mBuffers[0].mData,
	   abl.mBuffers[0].mDataByteSize/st->sampsz, arg);

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


int audiounit_recorder_alloc(struct ausrc_st **stp, const struct ausrc *as,
			     struct media_ctx **ctx,
			     struct ausrc_prm *prm, const char *device,
			     ausrc_read_h *rh, ausrc_error_h *errh, void *arg)
{
	AudioStreamBasicDescription fmt;
	const AudioUnitElement inputBus = 1;
	const AudioUnitElement outputBus = 0;
	AURenderCallbackStruct cb;
	struct ausrc_st *st;
	const UInt32 enable = 1;
	const UInt32 disable = 0;
#if ! TARGET_OS_IPHONE
	UInt32 ausize = sizeof(AudioDeviceID);
	AudioDeviceID inputDevice;
	AudioObjectPropertyAddress auAddress = {
		kAudioHardwarePropertyDefaultInputDevice,
		kAudioObjectPropertyScopeGlobal,
		kAudioObjectPropertyElementMaster };
#endif
	Float64 hw_srate = 0.0;
	UInt32 hw_size = sizeof(hw_srate);
	OSStatus ret = 0;
	int err;

	(void)ctx;
	(void)device;
	(void)errh;

	if (!stp || !as || !prm)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), ausrc_destructor);
	if (!st)
		return ENOMEM;

	st->as  = as;
	st->rh  = rh;
	st->arg = arg;
	st->ch  = prm->ch;

	st->sampsz = (uint32_t)aufmt_sample_size(prm->fmt);
	if (!st->sampsz) {
		err = ENOTSUP;
		goto out;
	}

	err = pthread_mutex_init(&st->mutex, NULL);
	if (err)
		goto out;

	err = audiosess_alloc(&st->sess, interrupt_handler, st);
	if (err)
		goto out;

	ret = AudioComponentInstanceNew(audiounit_comp, &st->au);
	if (ret)
		goto out;

	ret = AudioUnitSetProperty(st->au, kAudioOutputUnitProperty_EnableIO,
				   kAudioUnitScope_Input, inputBus,
				   &enable, sizeof(enable));
	if (ret)
		goto out;

#if ! TARGET_OS_IPHONE
	ret = AudioUnitSetProperty(st->au, kAudioOutputUnitProperty_EnableIO,
				   kAudioUnitScope_Output, outputBus,
				   &disable, sizeof(disable));
	if (ret)
		goto out;

	ret = AudioObjectGetPropertyData(kAudioObjectSystemObject,
			&auAddress,
			0,
			NULL,
			&ausize,
			&inputDevice);
	if (ret)
		goto out;

	ret = AudioUnitSetProperty(st->au,
			kAudioOutputUnitProperty_CurrentDevice,
			kAudioUnitScope_Global,
			0,
			&inputDevice,
			sizeof(inputDevice));
	if (ret)
		goto out;
#endif

	fmt.mSampleRate       = prm->srate;
	fmt.mFormatID         = kAudioFormatLinearPCM;
#if TARGET_OS_IPHONE
	fmt.mFormatFlags      = audiounit_aufmt_to_formatflags(prm->fmt)
		| kAudioFormatFlagsNativeEndian
		| kAudioFormatFlagIsPacked;
#else
	fmt.mFormatFlags      = audiounit_aufmt_to_formatflags(prm->fmt)
		| kLinearPCMFormatFlagIsPacked;
#endif
	fmt.mBitsPerChannel   = 8 * st->sampsz;
	fmt.mChannelsPerFrame = prm->ch;
	fmt.mBytesPerFrame    = st->sampsz * prm->ch;
	fmt.mFramesPerPacket  = 1;
	fmt.mBytesPerPacket   = st->sampsz * prm->ch;
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

	ret = AudioUnitGetProperty(st->au,
				   kAudioUnitProperty_SampleRate,
				   kAudioUnitScope_Input,
				   0,
				   &hw_srate,
				   &hw_size);
	if (ret)
		goto out;

	debug("audiounit: record hardware sample rate is now at %f Hz\n",
	      hw_srate);

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
