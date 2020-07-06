/**
 * @file jack_play.c  JACK audio driver -- player
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <jack/jack.h>
#include "mod_jack.h"


struct auplay_st {
	const struct auplay *ap;  /* pointer to base-class (inheritance) */

	struct auplay_prm prm;
	float *sampv;
	size_t sampc;             /* includes number of channels */
	auplay_write_h *wh;
	void *arg;

	jack_client_t *client;
	jack_port_t **portv;
	jack_nframes_t nframes;       /* num frames per port (channel) */
};


/**
 * The process callback for this JACK application is called in a
 * special realtime thread once for each audio cycle.
 *
 * This client does nothing more than copy data from its input
 * port to its output port. It will exit when stopped by
 * the user (e.g. using Ctrl-C on a unix-ish operating system)
 *
 * NOTE avoid memory allocations in this function
 */
static int process_handler(jack_nframes_t nframes, void *arg)
{
	struct auplay_st *st = arg;
	size_t sampc = nframes * st->prm.ch;
	size_t ch, j;

	/* 1. read data from app (signed 16-bit) interleaved */
	st->wh(st->sampv, sampc, st->arg);

	/* 2. convert from 16-bit to float and copy to Jack */

	/* 3. de-interleave [LRLRLRLR] -> [LLLLL]+[RRRRR] */
	for (ch = 0; ch < st->prm.ch; ch++) {

		jack_default_audio_sample_t *buffer;

		buffer = jack_port_get_buffer(st->portv[ch], st->nframes);

		for (j = 0; j < nframes; j++) {
			float samp = st->sampv[j*st->prm.ch + ch];
			buffer[j] = samp;
		}
	}

	return 0;
}


static void auplay_destructor(void *arg)
{
	struct auplay_st *st = arg;

	info("jack: destroy\n");

	if (st->client)
		jack_client_close(st->client);

	mem_deref(st->sampv);
	mem_deref(st->portv);
}

static int start_jack(struct auplay_st *st)
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
		st->client = jack_client_open(client_name,
			options, &status, server_name);
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
	info("jack: source unique name `%s' assigned\n", client_name);

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
		re_snprintf(buf, sizeof(buf), "output_%u", ch+1);

		st->portv[ch] = jack_port_register (st->client, buf,
						    JACK_DEFAULT_AUDIO_TYPE,
						    JackPortIsOutput, 0);
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

	/* Connect the ports.  You can't do this before the client is
	 * activated, because we can't make connections to clients
	 * that aren't running.  Note the confusing (but necessary)
	 * orientation of the driver backend ports: playback ports are
	 * "input" to the backend, and capture ports are "output" from
	 * it.
	 */

	if (jack_connect_ports) {
		info("jack: connecting default input ports\n");
		ports = jack_get_ports (st->client, NULL, NULL,
					JackPortIsInput | JackPortIsPhysical);
		if (ports == NULL) {
			warning("jack: no physical playback ports\n");
			return ENODEV;
		}

		/* Connect all physical ports. In case of for example mono
		 * audio with 2 physical playback ports, connect the
		 * single registered port to both physical port.
		 */
		ch = 0;
		for (unsigned i = 0; ports[i] != NULL; i++) {
			if (jack_connect (st->client,
					jack_port_name (st->portv[ch]),
						ports[i])) {
				warning("jack: cannot connect output ports\n");
			}

			++ch;
			if (ch >= st->prm.ch) {
				ch = 0;
			}
		}

		jack_free(ports);
	}

	return 0;
}


int jack_play_alloc(struct auplay_st **stp, const struct auplay *ap,
		    struct auplay_prm *prm, const char *device,
		    auplay_write_h *wh, void *arg)
{
	struct auplay_st *st;
	int err = 0;

	(void)device;

	if (!stp || !ap || !prm || !wh)
		return EINVAL;

	info("jack: play %uHz,%uch\n", prm->srate, prm->ch);

	if (prm->fmt != AUFMT_FLOAT) {
		warning("jack: playback: unsupported sample format (%s)\n",
			aufmt_name(prm->fmt));
		return ENOTSUP;
	}

	st = mem_zalloc(sizeof(*st), auplay_destructor);
	if (!st)
		return ENOMEM;

	st->prm = *prm;
	st->ap  = ap;
	st->wh  = wh;
	st->arg = arg;

	st->portv = mem_reallocarray(NULL, prm->ch, sizeof(*st->portv), NULL);
	if (!st->portv) {
		err = ENOMEM;
		goto out;
	}

	err = start_jack(st);
	if (err)
		goto out;


	info("jack: sampc=%zu\n", st->sampc);

 out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}
