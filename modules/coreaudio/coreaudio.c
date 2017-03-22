/**
 * @file coreaudio.c  Apple Coreaudio sound driver
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <AudioToolbox/AudioToolbox.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "coreaudio.h"


/**
 * @defgroup coreaudio coreaudio
 *
 * Audio driver module for OSX CoreAudio
 */


static struct auplay *auplay;
static struct ausrc *ausrc;


#if TARGET_OS_IPHONE && __IPHONE_OS_VERSION_MIN_REQUIRED >= __IPHONE_2_0
static void interruptionListener(void *data, UInt32 inInterruptionState)
{
	(void)data;

	/* XXX: implement this properly */

	if (inInterruptionState == kAudioSessionBeginInterruption) {
		debug("coreaudio: player interrupt: Begin\n");
	}
	else if (inInterruptionState == kAudioSessionEndInterruption) {
		debug("coreaudio: player interrupt: End\n");
	}
}


int audio_session_enable(void)
{
	OSStatus res;
	UInt32 category;

	res = AudioSessionInitialize(NULL, NULL, interruptionListener, 0);
	if (res && res != 1768843636)
		return ENODEV;

	category = kAudioSessionCategory_PlayAndRecord;
	res = AudioSessionSetProperty(kAudioSessionProperty_AudioCategory,
				      sizeof(category), &category);
	if (res) {
		warning("coreaudio: Audio Category: %d\n", res);
		return ENODEV;
	}

	res = AudioSessionSetActive(true);
	if (res) {
		warning("coreaudio: AudioSessionSetActive: %d\n", res);
		return ENODEV;
	}

	return 0;
}


void audio_session_disable(void)
{
	AudioSessionSetActive(false);
}
#else
int audio_session_enable(void)
{
	return 0;
}


void audio_session_disable(void)
{
}
#endif


static int module_init(void)
{
	int err;

	err  = auplay_register(&auplay, baresip_auplayl(),
			       "coreaudio", coreaudio_player_alloc);
	err |= ausrc_register(&ausrc, baresip_ausrcl(),
			      "coreaudio", coreaudio_recorder_alloc);

	return err;
}


static int module_close(void)
{
	auplay = mem_deref(auplay);
	ausrc  = mem_deref(ausrc);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(coreaudio) = {
	"coreaudio",
	"audio",
	module_init,
	module_close,
};
