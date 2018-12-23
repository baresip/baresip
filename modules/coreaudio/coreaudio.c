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


int coreaudio_enum_devices(const char *name, struct list *dev_list,
			    CFStringRef *uid, Boolean is_input)
{
	AudioObjectPropertyAddress propertyAddress = {
		kAudioHardwarePropertyDevices,
		kAudioObjectPropertyScopeGlobal,
		kAudioObjectPropertyElementMaster
	};

	AudioDeviceID *audioDevices = NULL;
	UInt32 dataSize = 0;
	UInt32 deviceCount;
	OSStatus status;

	int err = 0;

	if (!dev_list && !uid)
		return EINVAL;

	if (uid) {
		*uid = NULL;

		if (!str_isset(name))
			return 0;
	}

	status = AudioObjectGetPropertyDataSize(kAudioObjectSystemObject,
						&propertyAddress,
						0,
						NULL,
						&dataSize);
	if (kAudioHardwareNoError != status) {
		warning("AudioObjectGetPropertyDataSize"
			" (kAudioHardwarePropertyDevices) failed: %i\n",
			status);
		err = ENODEV;
		goto out;
	}

	deviceCount = dataSize / sizeof(AudioDeviceID);

	audioDevices = mem_zalloc(dataSize, NULL);
	if (NULL == audioDevices) {
		err = ENOMEM;
		goto out;
	}

	status = AudioObjectGetPropertyData(kAudioObjectSystemObject,
					    &propertyAddress,
					    0,
					    NULL,
					    &dataSize,
					    audioDevices);
	if (kAudioHardwareNoError != status) {
		warning("AudioObjectGetPropertyData"
			" (kAudioHardwarePropertyDevices) failed: %i\n",
			status);
		err = ENODEV;
		goto out;
	}

	if (is_input)
		propertyAddress.mScope = kAudioDevicePropertyScopeInput;
	else
		propertyAddress.mScope = kAudioDevicePropertyScopeOutput;

	for (UInt32 i = 0; i < deviceCount; ++i) {

		CFStringRef deviceUID = NULL;
		CFStringRef deviceName = NULL;
		const char *name_str;

		propertyAddress.mSelector   = kAudioDevicePropertyStreams;
		status = AudioObjectGetPropertyDataSize(audioDevices[i],
							&propertyAddress,
							0,
							NULL,
							&dataSize);
		if (dataSize == 0)
			continue;

		dataSize = sizeof(deviceUID);
		propertyAddress.mSelector = kAudioDevicePropertyDeviceUID;
		status = AudioObjectGetPropertyData(audioDevices[i],
						    &propertyAddress,
						    0,
						    NULL,
						    &dataSize,
						    &deviceUID);
		if (kAudioHardwareNoError != status) {
			warning("AudioObjectGetPropertyData"
				" (kAudioDevicePropertyDeviceUID) "
				"failed: %i\n", status);
			continue;
		}

		dataSize = sizeof(deviceName);
		propertyAddress.mSelector =
			kAudioDevicePropertyDeviceNameCFString;

		status = AudioObjectGetPropertyData(audioDevices[i],
						    &propertyAddress,
						    0,
						    NULL,
						    &dataSize,
						    &deviceName);
		if (kAudioHardwareNoError != status) {
			warning("AudioObjectGetPropertyData"
				" (kAudioDevicePropertyDeviceNameCFString)"
				" failed: %i\n", status);
			continue;
		}

		name_str = CFStringGetCStringPtr(deviceName,
						 kCFStringEncodingUTF8);

		if (uid) {
			if (0 == str_casecmp(name, name_str)) {
				*uid = deviceUID;
				break;
			}
		}
		else {
			err = mediadev_add(dev_list, name_str);
			if (err)
				break;
		}
	}

 out:
	mem_deref(audioDevices);

	return err;
}


static int module_init(void)
{
	int err;

	err  = auplay_register(&auplay, baresip_auplayl(),
			       "coreaudio", coreaudio_player_alloc);
	err |= ausrc_register(&ausrc, baresip_ausrcl(),
			      "coreaudio", coreaudio_recorder_alloc);

	if (err)
		return err;

	err  = coreaudio_player_init(auplay);
	err |= coreaudio_recorder_init(ausrc);

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
