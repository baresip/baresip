/**
 * @file src/aulevel.c  Audio level
 *
 * Copyright (C) 2017 Creytiv.com
 */

#include <math.h>
#include <re.h>
#include <baresip.h>
#include "core.h"


/**
 * Generic routine to calculate RMS (Root-Mean-Square) from
 * a set of signed 16-bit values
 *
 * \verbatim

	     .---------------
	     |   N-1
	     |  ----.
	     |  \
	     |   \        2
	     |    |   s[n]
	     |   /
	     |  /
	 _   |  ----'
	  \  |   n=0
	   \ |  ------------
	    \|       N

   \endverbatim
 *
 * @param data Array of signed 16-bit values
 * @param len  Number of values
 *
 * @return RMS value from 0 to 32768
 */
static double calc_rms(const int16_t *data, size_t len)
{
	double sum = 0;
	size_t i;

	if (!data || !len)
		return .0;

	for (i = 0; i < len; i++) {
		const double sample = data[i];

		sum += sample * sample;
	}

	return sqrt(sum / (double)len);
}


/**
 * Calculate the audio level in dBov from a set of audio samples.
 * dBov is the level, in decibels, relative to the overload point
 * of the system
 *
 * @param sampv Audio samples
 * @param sampc Number of audio samples
 *
 * @return Audio level expressed in dBov
 */
double aulevel_calc_dbov(const int16_t *sampv, size_t sampc)
{
	static const double peak = 32767.0;
	double rms, dbov;

	if (!sampv || !sampc)
		return AULEVEL_MIN;

	rms = calc_rms(sampv, sampc) / peak;

	dbov = 20 * log10(rms);

	if (dbov < AULEVEL_MIN)
		dbov = AULEVEL_MIN;
	else if (dbov > AULEVEL_MAX)
		dbov = AULEVEL_MAX;

	return dbov;
}
