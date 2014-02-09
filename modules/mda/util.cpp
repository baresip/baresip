/**
 * @file util.cpp  Symbian MDA audio driver -- utilities
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <e32def.h>
#include <e32std.h>
#include <mda/common/audio.h>

extern "C" {
#include <re.h>
#include <baresip.h>
#include "mda.h"
}


int convert_srate(uint32_t srate)
{
	switch (srate) {

	case 8000:  return TMdaAudioDataSettings::ESampleRate8000Hz;
	case 12000: return TMdaAudioDataSettings::ESampleRate12000Hz;
	case 16000: return TMdaAudioDataSettings::ESampleRate16000Hz;
	case 24000: return TMdaAudioDataSettings::ESampleRate24000Hz;
	case 32000: return TMdaAudioDataSettings::ESampleRate32000Hz;
	default:    return -1;
	}
}


int convert_channels(uint8_t ch)
{
	switch (ch) {

	case 1:  return TMdaAudioDataSettings::EChannelsMono;
	case 2:  return TMdaAudioDataSettings::EChannelsStereo;
	default: return -1;
	}
}
