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
		/* fallback if CFStringGetCStringPtr fails */
		char name_buf[64];

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

		/* CFStringGetCStringPtr can and does fail
		 * (documented behavior) */
		if (0 == name_str) {
			if (!CFStringGetCString(deviceName,
						name_buf,
						sizeof(name_buf),
						kCFStringEncodingUTF8)) {
				warning("CFStringGetCString "
					" failed: %i\n", status);
				continue;
			}
			name_str = name_buf;
		}

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


uint32_t coreaudio_aufmt_to_formatflags(enum aufmt fmt)
{
	switch (fmt) {

	case AUFMT_S16LE:  return kLinearPCMFormatFlagIsSignedInteger;
	case AUFMT_S24_3LE:return kLinearPCMFormatFlagIsSignedInteger;
	case AUFMT_FLOAT:  return kLinearPCMFormatFlagIsFloat;
	default: return 0;
	}
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
