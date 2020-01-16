/**
 * @file audiounit.c  AudioUnit sound driver
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <AudioUnit/AudioUnit.h>
#include <AudioToolbox/AudioToolbox.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "audiounit.h"


/**
 * @defgroup audiounit audiounit
 *
 * Audio driver module for OSX/iOS AudioUnit
 */


#define MAX_NB_FRAMES 4096


struct conv_buf {
	void *mem[2];
	uint8_t mem_idx;
	uint32_t nb_frames;
};


AudioComponent audiounit_io = NULL;
AudioComponent audiounit_conv = NULL;

static struct auplay *auplay;
static struct ausrc *ausrc;


static void conv_buf_destructor(void *arg)
{
	struct conv_buf *buf = (struct conv_buf *)arg;

	mem_deref(buf->mem[0]);
	mem_deref(buf->mem[1]);
}


int conv_buf_alloc(struct conv_buf **bufp, size_t framesz)
{
	struct conv_buf *buf;

	if (!bufp)
		return EINVAL;

	buf = mem_zalloc(sizeof(*buf), conv_buf_destructor);
	if (!buf)
		return ENOMEM;

	buf->mem_idx = 0;
	buf->nb_frames = 0;
	buf->mem[0] = mem_alloc(MAX_NB_FRAMES * framesz, NULL);
	buf->mem[1] = mem_alloc(MAX_NB_FRAMES * framesz, NULL);

	*bufp = buf;

	return 0;
}


int  get_nb_frames(struct conv_buf *buf, uint32_t *nb_frames)
{
	if (!buf)
		return EINVAL;

	*nb_frames = buf->nb_frames;

	return 0;
}


OSStatus init_data_write(struct conv_buf *buf, void **data,
			 size_t framesz, uint32_t nb_frames)
{
	uint32_t mem_idx = buf->mem_idx;

	if (buf->nb_frames + nb_frames > MAX_NB_FRAMES) {
		return kAudioUnitErr_TooManyFramesToProcess;
	}

	*data = (uint8_t*)buf->mem[mem_idx] +
		buf->nb_frames * framesz;

	buf->nb_frames = buf->nb_frames + nb_frames;

	return noErr;
}


OSStatus init_data_read(struct conv_buf *buf, void **data,
			size_t framesz, uint32_t nb_frames)
{
	uint8_t *src;
	uint32_t delta = 0;
	uint32_t mem_idx = buf->mem_idx;

	if (buf->nb_frames < nb_frames) {
		return kAudioUnitErr_TooManyFramesToProcess;
	}

	*data = buf->mem[mem_idx];

	delta = buf->nb_frames - nb_frames;

	src = (uint8_t *)buf->mem[mem_idx] + nb_frames * framesz;

	memcpy(buf->mem[(mem_idx+1)%2],
	       (void *)src, delta * framesz);

	buf->mem_idx = (mem_idx + 1)%2;
	buf->nb_frames = delta;

	return noErr;
}


uint32_t audiounit_aufmt_to_formatflags(enum aufmt fmt)
{
	switch (fmt) {

	case AUFMT_S16LE:  return kLinearPCMFormatFlagIsSignedInteger;
	case AUFMT_S24_3LE:return kLinearPCMFormatFlagIsSignedInteger;
	case AUFMT_FLOAT:  return kLinearPCMFormatFlagIsFloat;
	default: return 0;
	}
}


#if TARGET_OS_IPHONE
static void interruptionListener(void *data, UInt32 inInterruptionState)
{
	(void)data;

	if (inInterruptionState == kAudioSessionBeginInterruption) {
		info("audiounit: interrupt Begin\n");
		audiosess_interrupt(true);
	}
	else if (inInterruptionState == kAudioSessionEndInterruption) {
		info("audiounit: interrupt End\n");
		audiosess_interrupt(false);
	}
}
#endif


static int module_init(void)
{
	AudioComponentDescription desc;
	CFStringRef name = NULL;
	int err;

#if TARGET_OS_IPHONE
	OSStatus ret;

	ret = AudioSessionInitialize(NULL, NULL, interruptionListener, 0);
	if (ret && ret != kAudioSessionAlreadyInitialized) {
		warning("audiounit: AudioSessionInitialize: %d\n", ret);
		return ENODEV;
	}
#endif

	desc.componentType = kAudioUnitType_Output;
#if TARGET_OS_IPHONE
	desc.componentSubType = kAudioUnitSubType_VoiceProcessingIO;
#else
	desc.componentSubType = kAudioUnitSubType_HALOutput;
#endif
	desc.componentManufacturer = kAudioUnitManufacturer_Apple;
	desc.componentFlags = 0;
	desc.componentFlagsMask = 0;

	audiounit_comp_io = AudioComponentFindNext(NULL, &desc);
	if (!audiounit_comp_io) {
#if TARGET_OS_IPHONE
		warning("audiounit: Voice Processing I/O not found\n");
#else
		warning("audiounit: AUHAL not found\n");
#endif
		return ENOENT;
	}

	if (0 == AudioComponentCopyName(audiounit_comp_io, &name)) {
		debug("audiounit: using component '%s'\n",
		      CFStringGetCStringPtr(name, kCFStringEncodingUTF8));
	}

	desc.componentType = kAudioUnitType_FormatConverter;
	desc.componentSubType = kAudioUnitSubType_AUConverter;
	desc.componentManufacturer = kAudioUnitManufacturer_Apple;
	desc.componentFlags = 0;
	desc.componentFlagsMask = 0;

	audiounit_comp_conv = AudioComponentFindNext(NULL, &desc);
	if (!audiounit_comp_conv) {
		warning("audiounit: AU Converter not found\n");
		return ENOENT;
	}

	if (0 == AudioComponentCopyName(audiounit_comp_conv, &name)) {
		debug("audiounit: using component '%s'\n",
		      CFStringGetCStringPtr(name, kCFStringEncodingUTF8));
	}

	err  = auplay_register(&auplay, baresip_auplayl(),
			       "audiounit", audiounit_player_alloc);
	err |= ausrc_register(&ausrc, baresip_ausrcl(),
			      "audiounit", audiounit_recorder_alloc);

	return err;
}


static int module_close(void)
{
	ausrc  = mem_deref(ausrc);
	auplay = mem_deref(auplay);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(audiounit) = {
	"audiounit",
	"audio",
	module_init,
	module_close,
};
