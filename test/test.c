/**
 * @file test.c  Selftest for Baresip core
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <math.h>
#include <re.h>
#include <baresip.h>
#include "test.h"


static void timeout_handler(void *arg)
{
	int *err = arg;

	warning("selftest: re_main() loop timed out -- test hung..\n");

	*err = ETIMEDOUT;

	re_cancel();
}


static void signal_handler(int sig)
{
	re_fprintf(stderr, "test interrupted by signal %d\n", sig);
	re_cancel();
}


int re_main_timeout(uint32_t timeout_ms)
{
	struct tmr tmr;
	int err = 0;

	tmr_init(&tmr);

	tmr_start(&tmr, timeout_ms, timeout_handler, &err);
	re_main(signal_handler);

	tmr_cancel(&tmr);
	return err;
}


bool test_cmp_double(double a, double b, double precision)
{
	return fabs(a - b) < precision;
}


void test_hexdump_dual(FILE *f,
		       const void *ep, size_t elen,
		       const void *ap, size_t alen)
{
	const uint8_t *ebuf = ep;
	const uint8_t *abuf = ap;
	size_t i, j, len;
#define WIDTH 8

	if (!f || !ep || !ap)
		return;

	len = max(elen, alen);

	(void)re_fprintf(f, "\nOffset:   Expected (%zu bytes):    "
			 "   Actual (%zu bytes):\n", elen, alen);

	for (i=0; i < len; i += WIDTH) {

		(void)re_fprintf(f, "0x%04zx   ", i);

		for (j=0; j<WIDTH; j++) {
			const size_t pos = i+j;
			if (pos < elen) {
				bool wrong = pos >= alen;

				if (wrong)
					(void)re_fprintf(f, "\x1b[35m");
				(void)re_fprintf(f, " %02x", ebuf[pos]);
				if (wrong)
					(void)re_fprintf(f, "\x1b[;m");
			}
			else
				(void)re_fprintf(f, "   ");
		}

		(void)re_fprintf(f, "    ");

		for (j=0; j<WIDTH; j++) {
			const size_t pos = i+j;
			if (pos < alen) {
				bool wrong;

				if (pos < elen)
					wrong = ebuf[pos] != abuf[pos];
				else
					wrong = true;

				if (wrong)
					(void)re_fprintf(f, "\x1b[33m");
				(void)re_fprintf(f, " %02x", abuf[pos]);
				if (wrong)
					(void)re_fprintf(f, "\x1b[;m");
			}
			else
				(void)re_fprintf(f, "   ");
		}

		(void)re_fprintf(f, "\n");
	}

	(void)re_fprintf(f, "\n");
}
