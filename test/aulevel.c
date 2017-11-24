/**
 * @file test/aulevel.c  Baresip selftest -- audio levels
 *
 * Copyright (C) 2010 - 2017 Creytiv.com
 */

#include <re.h>
#include <baresip.h>
#include "test.h"


#define DEBUG_MODULE "aulevel"
#define DEBUG_LEVEL 5
#include <re_dbg.h>


#define PREC .6


int test_aulevel(void)
{
	static const struct {
		int16_t sampv[2];
		double level;
	} testv[] = {

		{  {    0,     -0},    -96.0  },
		{  {    0,      1},    -93.0  },
		{  {    1,     -1},    -90.0  },
		{  {    2,     -2},    -84.0  },
		{  {    4,     -4},    -78.0  },
		{  {    8,     -8},    -72.0  },
		{  {   16,    -16},    -66.0  },
		{  {   32,    -32},    -60.0  },
		{  {   64,    -64},    -54.0  },
		{  {  128,   -128},    -48.0  },
		{  {  256,   -256},    -42.0  },
		{  {  512,   -512},    -36.0  },
		{  { 1024,  -1024},    -30.0  },
		{  { 2048,  -2048},    -24.0  },
		{  { 4096,  -4096},    -18.0  },
		{  { 8192,  -8192},    -12.0  },
		{  {16384, -16384},     -6.0  },
		{  {32767, -32768},      0.0  },
	};
	size_t i;
	int err = 0;

	for (i=0; i<ARRAY_SIZE(testv); i++) {

		double level;

		level = aulevel_calc_dbov(testv[i].sampv,
					  ARRAY_SIZE(testv[i].sampv));

		ASSERT_DOUBLE_EQ(testv[i].level, level, PREC);
	}

 out:
	return err;
}
