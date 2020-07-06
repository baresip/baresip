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
	float *sampv;
	size_t sampc;             /* includes number of channels */
	ausrc_read_h *rh;
	void *arg;

	jack_client_t *client;
	jack_port_t **portv;
	jack_nframes_t nframes;       /* num frames per port (channel) */
};


static int process_handler(jack_nframes_t nframes, void *arg)
{
	struct ausrc_st *st = arg;
	struct auframe af;
	size_t sampc = nframes * st->prm.ch;
	size_t ch, j;
	uint64_t ts;

	ts = jack_frames_to_time(st->client, jack_last_frame_time(st->client));

	/* 2. convert from 16-bit to float and copy to Jack */

	/* 3. de-interleave [LRLRLRLR] -> [LLLLL]+[RRRRR] */
	for (ch = 0; ch < st->prm.ch; ch++) {

		const jack_default_audio_sample_t *buffer;

		buffer = jack_port_get_buffer(st->portv[ch], st->nframes);

		for (j = 0; j < nframes; j++) {
			float samp = buffer[j];
			st->sampv[j*st->prm.ch + ch] = samp;
		}
	}

	af.fmt   = st->prm.fmt;
	af.sampv = st->sampv;
	af.sampc = sampc;
	af.timestamp = ts;

	/* 1. read data from app (signed 16-bit) interleaved */
	st->rh(&af, st->arg);

	return 0;
}


static void ausrc_destructor(void *arg)
{
	struct ausrc_st *st = arg;

	info("jack: source destroy\n");

	if (st->client)
		jack_client_close(st->client);

	mem_deref(st->sampv);
	mem_deref(st->portv);
}


static int start_jack(struct ausrc_st *st)
{
	struct conf *conf = conf_cur();
	const char **ports;
	const char *client_name = "baresip";
	const char *server_name = NULL;
	jack_options_t options = JackNullOption;
	jack_status_t status;
	unsigned ch;
	jack_nframes_t engine_srate;

	bool jack_connect_ports = true;
	(void)conf_get_bool(conf, "jack_connect_ports",
				&jack_connect_ports);

	/* open a client connection to the JACK server */
	size_t len = jack_client_name_size();
	char *conf_name = mem_alloc(len+1, NULL);

	if (!conf_get_str(conf, "jack_client_name",
			conf_name, len)) {
		st->client = jack_client_open(conf_name, options,
						&status, server_name);
	}
	else {
		st->client = jack_client_open(client_name, options,
							&status, server_name);
	}
	mem_deref(conf_name);

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
	client_name = jack_get_client_name(st->client);
	info("jack: destination unique name `%s' assigned\n", client_name);

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

	st->sampc = st->nframes * st->prm.ch;
	st->sampv = mem_alloc(st->sampc * sizeof(float), NULL);
	if (!st->sampv)
		return ENOMEM;

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

	if (jack_connect_ports) {
		info("jack: connecting default output ports\n");
		ports = jack_get_ports (st->client, NULL, NULL,
					JackPortIsOutput | JackPortIsPhysical);
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
	}

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

	if (prm->fmt != AUFMT_FLOAT) {
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

	st->portv = mem_reallocarray(NULL, prm->ch, sizeof(*st->portv), NULL);
	if (!st->portv) {
		err = ENOMEM;
		goto out;
	}

	err = start_jack(st);
	if (err)
		goto out;

	info("jack: source sampc=%zu\n", st->sampc);

 out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}
