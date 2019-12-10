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

#ifndef __DTMF_GEN_H__
#define	__DTMF_GEN_H__

#include <stdint.h>

#define	DTMF_MAX_DIGITS 128

struct dtmf_to_freq {
	uint16_t f0;			/* Hz */
	uint16_t f1;			/* Hz */
	uint8_t	key;
};

struct dtmf_state {
	float	kx[2];
	float	ky[2];
	uint32_t duration;
};

struct dtmf_generator {
	struct dtmf_state state[DTMF_MAX_DIGITS];
	volatile uint16_t input_pos;
	volatile uint16_t output_pos;

	float	x[2];
	float	y[2];
	uint32_t duration;
};

static inline int
dtmf_is_empty(struct dtmf_generator *pg)
{
	return (pg->input_pos == pg->output_pos);
}

int	dtmf_queue_digit(struct dtmf_generator *, uint8_t digit,
			 uint32_t sample_rate, uint16_t tone_duration,
			 uint16_t gap_duration);
int	dtmf_get_sample(struct dtmf_generator *pg, int16_t *psamp);

#endif			/* __DTMF_GEN_H__ */
