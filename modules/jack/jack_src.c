/**
 * @file jack_src.c  JACK audio driver -- source
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <math.h>
#include <jack/jack.h>
#include "mod_jack.h"


struct ausrc_st {
	const struct ausrc *as;  /* pointer to base-class (inheritance) */

	struct ausrc_prm prm;
	int16_t *sampv;
	size_t sampc;             /* includes number of channels */
	ausrc_read_h *rh;
	void *arg;

	jack_client_t *client;
	jack_port_t *portv[2];
	jack_nframes_t nframes;       /* num frames per port (channel) */
};


static inline int16_t ausamp_float2short(float in)
{
	double scaled_value;
	int16_t out;

	scaled_value = in * (8.0 * 0x10000000);

	if (scaled_value >= (1.0 * 0x7fffffff)) {
		out = 32767;
	}
	else if (scaled_value <= (-8.0 * 0x10000000)) {
		out = -32768;
	}
	else
		out = (short) (lrint (scaled_value) >> 16);

	return out;
}


static int process_handler(jack_nframes_t nframes, void *arg)
{
	struct ausrc_st *st = arg;
	size_t sampc = nframes * st->prm.ch;
	size_t ch, j;

	/* 2. convert from 16-bit to float and copy to Jack */

	/* 3. de-interleave [LRLRLRLR] -> [LLLLL]+[RRRRR] */
	for (ch = 0; ch < st->prm.ch; ch++) {

		const jack_default_audio_sample_t *buffer;

		buffer = jack_port_get_buffer(st->portv[ch], st->nframes);

		for (j = 0; j < nframes; j++) {
			int16_t samp;
			samp = ausamp_float2short(buffer[j]);
			st->sampv[j*st->prm.ch + ch] = samp;
		}
	}

	/* 1. read data from app (signed 16-bit) interleaved */
	st->rh(st->sampv, sampc, st->arg);

	return 0;
}


static void ausrc_destructor(void *arg)
{
	struct ausrc_st *st = arg;

	info("jack: source destroy\n");

	if (st->client)
		jack_client_close(st->client);

	mem_deref(st->sampv);
}


static int start_jack(struct ausrc_st *st)
{
	const char **ports;
	const char *client_name = "baresip";
	const char *server_name = NULL;
	jack_options_t options = JackNullOption;
	jack_status_t status;
	unsigned ch;
	jack_nframes_t engine_srate;

	/* open a client connection to the JACK server */

	st->client = jack_client_open(client_name, options,
				      &status, server_name);
	if (st->client == NULL) {
		warning("jack: jack_client_open() failed, "
			"status = 0x%2.0x\n", status);

		if (status & JackServerFailed) {
			warning("jack: Unable to connect to JACK server\n");
		}
		return ENODEV;
	}
	if (status & JackServerStarted) {
		info("jack: JACK server started\n");
	}
	if (status & JackNameNotUnique) {
		client_name = jack_get_client_name(st->client);
		info("jack: unique name `%s' assigned\n", client_name);
	}

	jack_set_process_callback(st->client, process_handler, st);

	engine_srate = jack_get_sample_rate(st->client);
	st->nframes  = jack_get_buffer_size(st->client);

	info("jack: engine sample rate: %" PRIu32 " max_frames=%u\n",
	     engine_srate, st->nframes);

	/* currently the application must use the same sample-rate
	   as the jack server backend */
	if (engine_srate != st->prm.srate) {
		warning("jack: samplerate %uHz expected\n", engine_srate);
		return EINVAL;
	}

	/* create one port per channel */
	for (ch=0; ch<st->prm.ch; ch++) {

		char buf[32];
		re_snprintf(buf, sizeof(buf), "input_%u", ch+1);

		st->portv[ch] = jack_port_register(st->client, buf,
						   JACK_DEFAULT_AUDIO_TYPE,
						   JackPortIsInput, 0);
		if ( st->portv[ch] == NULL) {
			warning("jack: no more JACK ports available\n");
			return ENODEV;
		}
	}

	/* Tell the JACK server that we are ready to roll.  Our
	 * process() callback will start running now. */

	if (jack_activate (st->client)) {
		warning("jack: cannot activate client");
		return ENODEV;
	}

	ports = jack_get_ports (st->client, NULL, NULL,
				JackPortIsOutput);
	if (ports == NULL) {
		warning("jack: no physical playback ports\n");
		return ENODEV;
	}

	for (ch=0; ch<st->prm.ch; ch++) {

		if (jack_connect(st->client, ports[ch],
				 jack_port_name(st->portv[ch]))) {
			warning("jack: cannot connect output ports\n");
		}
	}

	jack_free(ports);

	return 0;
}


int jack_src_alloc(struct ausrc_st **stp, const struct ausrc *as,
		   struct media_ctx **ctx,
		   struct ausrc_prm *prm, const char *device,
		   ausrc_read_h *rh, ausrc_error_h *errh, void *arg)
{
	struct ausrc_st *st;
	int err = 0;

	(void)ctx;
	(void)device;
	(void)errh;

	if (!stp || !as || !prm || !rh)
		return EINVAL;

	if (prm->ch > ARRAY_SIZE(st->portv))
		return EINVAL;

	if (prm->fmt != AUFMT_S16LE) {
		warning("jack: source: unsupported sample format (%s)\n",
			aufmt_name(prm->fmt));
		return ENOTSUP;
	}

	st = mem_zalloc(sizeof(*st), ausrc_destructor);
	if (!st)
		return ENOMEM;

	st->prm = *prm;
	st->as  = as;
	st->rh  = rh;
	st->arg = arg;

	err = start_jack(st);
	if (err)
		goto out;

	st->sampc = st->nframes * prm->ch;
	st->sampv = mem_alloc(st->sampc * sizeof(int16_t), NULL);
	if (!st->sampv) {
		err = ENOMEM;
		goto out;
	}

	info("jack: source sampc=%zu\n", st->sampc);

 out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}
