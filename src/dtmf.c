/*-
 * Copyright (c) 2019 Hans Petter Selasky. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "dtmf.h"

#include <errno.h>
#include <math.h>

static const struct dtmf_to_freq dtmf_to_freq[] = {
	{941, 1477, '#'},
	{941, 1209, '*'},
	{941, 1336, '0'},
	{697, 1209, '1'},
	{697, 1336, '2'},
	{697, 1477, '3'},
	{770, 1209, '4'},
	{770, 1336, '5'},
	{770, 1477, '6'},
	{852, 1209, '7'},
	{852, 1336, '8'},
	{852, 1477, '9'},
	{697, 1633, 'A'},
	{770, 1633, 'B'},
	{852, 1633, 'C'},
	{941, 1633, 'D'},
	{0, 0, 0},
};

static void
dtmf_set_state(struct dtmf_state *ps, uint32_t sample_rate,
	       uint32_t duration, uint16_t f0, uint16_t f1)
{
	const float two_pi = 2.0 * 3.14159265358979323846;
	float p0 = two_pi * (float)f0 / (float)sample_rate;
	float p1 = two_pi * (float)f1 / (float)sample_rate;

	ps->kx[0] = cosf(p0);
	ps->ky[0] = sinf(p0);
	ps->kx[1] = cosf(p1);
	ps->ky[1] = sinf(p1);

	ps->duration = duration;
}

int
dtmf_queue_digit(struct dtmf_generator *pg,
		 uint8_t digit, uint32_t sample_rate,
		 uint16_t tone_duration, uint16_t gap_duration)
{
	uint32_t next_pos0 = (pg->output_pos + 1) % DTMF_MAX_DIGITS;
	uint32_t next_pos1 = (pg->output_pos + 2) % DTMF_MAX_DIGITS;
	uint32_t n;

	if (next_pos0 == pg->input_pos || next_pos1 == pg->input_pos)
		return (ENOMEM);

	if (tone_duration > 0x1fff)
		tone_duration = 0x1fff;
	else if (tone_duration == 0)
		tone_duration = 40;	/* default in milliseconds */

	/* convert to number of samples */
	tone_duration = (tone_duration * sample_rate) / 1000;

	if (gap_duration > 0x1fff)
		gap_duration = 0x1fff;
	else if (gap_duration == 0)
		gap_duration = 40;	/* default in milliseconds */

	/* convert to number of samples */
	gap_duration = (gap_duration * sample_rate) / 1000;

	/* lookup the frequency */
	for (n = 0;; n++) {
		if ((dtmf_to_freq[n].key == digit) ||
		    (dtmf_to_freq[n].key == 0)) {
			break;
		}
	}

	dtmf_set_state(pg->state + pg->output_pos,
	    sample_rate, tone_duration,
	    dtmf_to_freq[n].f0, dtmf_to_freq[n].f1);

	dtmf_set_state(pg->state + next_pos0,
	    sample_rate, gap_duration, 0, 0);

	pg->output_pos = next_pos1;

	return (0);
}

static int16_t
dtmf_compute_sample(struct dtmf_generator *pg, const struct dtmf_state *ps)
{
	/* complex multiplication */
	float nx[2] = {
		pg->x[0] * ps->kx[0] - pg->y[0] * ps->ky[0],
		pg->x[1] * ps->kx[1] - pg->y[1] * ps->ky[1]
	};
	float ny[2] = {
		pg->y[0] * ps->kx[0] + pg->x[0] * ps->ky[0],
		pg->y[1] * ps->kx[1] + pg->x[1] * ps->ky[1]
	};
	float ret = pg->y[0] + pg->y[1];

	pg->x[0] = nx[0];
	pg->y[0] = ny[0];

	pg->x[1] = nx[1];
	pg->y[1] = ny[1];

	pg->duration++;

	return (ret * 8192.0f);
}

int
dtmf_get_sample(struct dtmf_generator *pg, int16_t *psamp)
{
top:
	if (pg->input_pos == pg->output_pos) {
		return (EINVAL);
	}
	else if (pg->duration == 0) {
		pg->x[0] = 1.0f;
		pg->x[1] = 1.0f;
		pg->y[0] = 0.0f;
		pg->y[1] = 0.0f;
	}
	else if (pg->duration >= pg->state[pg->input_pos].duration) {
		pg->duration = 0;
		pg->input_pos = (pg->input_pos + 1) % DTMF_MAX_DIGITS;
		goto top;
	}
	*psamp = dtmf_compute_sample(pg, pg->state + pg->input_pos);
	return (0);
}
