/**
 * @file src/mos.c  MOS (Mean Opinion Score)
 *
 * Copyright (C) 2010 - 2016 Creytiv.com
 */
#include <re.h>
#include <baresip.h>
#include "core.h"


static double rfactor_to_mos(double r)
{
	double mos;

	mos = 1 + (0.035) * (r) + (0.000007) * (r) * ((r) - 60) * (100 - (r));

	if (mos > 5)
		mos = 5;

	return mos;
}


/**
 * Calculate Pseudo-MOS (Mean Opinion Score)
 *
 * @param r_factor          Pointer to where R-factor is written (optional)
 * @param rtt               Average roundtrip time
 * @param jitter            Jitter
 * @param num_packets_lost  Number of packets lost
 *
 * @return The calculated MOS value from 1 to 5
 *
 * Reference:  https://metacpan.org/pod/Algorithm::MOS
 */
double mos_calculate(double *r_factor, double rtt,
		     double jitter, uint32_t num_packets_lost)
{
	double effective_latency = rtt + (jitter * 2) + 10;
	double mos_val;
	double r;

	if (effective_latency < 160) {
		r = 93.2 - (effective_latency / 40);
	}
	else {
		r = 93.2 - (effective_latency - 120) / 10;
	}

	r = r - (num_packets_lost * 2.5);

	if (r > 100)
		r = 100;
	else if (r < 0)
		r = 0;

	mos_val = rfactor_to_mos(r);

	if (r_factor)
		*r_factor = r;

	return mos_val;
}
