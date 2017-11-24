/**
 * @file test/mos.c  Test the MOS (Mean Opinion Score) calculator
 *
 * Copyright (C) 2010 - 2016 Creytiv.com
 */
#include <re.h>
#include <baresip.h>
#include "test.h"


#define DEBUG_MODULE "mos"
#define DEBUG_LEVEL 5
#include <re_dbg.h>


int test_mos(void)
{
#define PRECISION 0.001
	static struct {
		/* input: */
		double rtt;
		double jitter;
		uint32_t packet_loss;

		/* output: */
		double r_factor;
		double mos;
	} testv[] = {
		{    0.0,    0.0,   0,  92.95,  4.404 },
		{  500.0,    0.0,   0,  54.20,  2.796 },
		{ 1000.0,    0.0,   0,   4.20,  0.990 },
		{    0.0,  100.0,   0,  84.20,  4.172 },
		{    0.0,  200.0,   0,  64.20,  3.315 },
		{    0.0,    0.0,   1,  90.45,  4.350 },
		{    0.0,    0.0,  10,  67.95,  3.499 },
		{   10.0,   10.0,  10,  67.20,  3.463 },
	};
	size_t i;
	int err = 0;

	for (i=0; i<ARRAY_SIZE(testv); i++) {
		double r_factor, mos;

		mos = mos_calculate(&r_factor, testv[i].rtt, testv[i].jitter,
				    testv[i].packet_loss);

		ASSERT_DOUBLE_EQ(testv[i].r_factor, r_factor, PRECISION);
		ASSERT_DOUBLE_EQ(testv[i].mos, mos, PRECISION);
	}

 out:
	return err;
}
