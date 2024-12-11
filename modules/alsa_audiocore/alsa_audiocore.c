/**
 * @file alsa_audiocore.c Acoustic Echo Cancellation and Noise Reduction
 *
 * Copyright (C) 2022 Commend.com - h.ramoser@commend.com
 */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <alsa/asoundlib.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <pthread.h>
#include "alsa_audiocore.h"

char alsa_ac_default_dev[64] = "default";
char alsa_ac_module_name[64] = "alsa_audiocore";

static struct ausrc *ausrc = NULL;
static struct auplay *auplay = NULL;

#define MSEC_TO_USEC 1000U
#define FRAMES_1_MSEC 16U

struct alsa_audiocore_st {
	pthread_t thread;
	volatile bool run;
	struct alsa_play_st* alsa_play;
	struct alsa_src_st* alsa_src;
	char alsa_play_device[256];
	char alsa_src_device[256];
	struct auplay_prm alsa_play_prm;
	struct ausrc_prm alsa_src_prm;
	void *alsa_play_sampv;
	size_t alsa_play_sampc;
	size_t alsa_play_num_frames;
	void *alsa_src_sampv;
	size_t alsa_src_sampc;
	size_t alsa_src_num_frames;
	struct auframe alsa_play_af;
	struct auframe alsa_src_af;
	volatile bool play_started;
	volatile bool src_started;
	struct auplay_st* play;
	struct ausrc_st* src;
	struct aubuf* src_aubuf;	/* processed mic data */
	struct aubuf* play_aubuf;	/* incoming LS data */
	pthread_mutex_t mutex;
};

struct ausrc_st {
	struct ausrc_prm prm;
	ausrc_read_h *rh;
	void* arg;
	size_t sampc;
	void *sampv;
	struct auframe af;
};

struct auplay_st {
	struct auplay_prm prm;
	auplay_write_h *wh;
	void* arg;
	size_t sampc;
	void *sampv;
	struct auframe af;
};

struct alsa_audiocore_st *m = NULL;

static void start_alsa_devices(void)
{
	int err;

	/* sync device start */
	err = snd_pcm_reset(m->alsa_src->read);
	if (err) {
		warning("alsa_audiocore: could not reset ALSA source (%s)\n",
				snd_strerror(err));
		return;
	}
	err = snd_pcm_start(m->alsa_src->read);
	if (err) {
		warning("alsa_audiocore: could not start ALSA source (%s)\n",
				snd_strerror(err));
		return;
	}
	err = snd_pcm_reset(m->alsa_play->write);
	if (err) {
		warning("alsa_audiocore: could not reset ALSA play (%s)\n",
				snd_strerror(err));
		return;
	}
	err = snd_pcm_prepare(m->alsa_play->write);
	if (err) {
		warning("alsa_audiocore: could not prepare ALSA play (%s)\n",
				snd_strerror(err));
		return;
	}
}


static void empty_src(void)
{
	/* empty src buffer */
	snd_pcm_uframes_t avail;

	while ((avail = snd_pcm_avail(m->alsa_src->read)) >= FRAMES_1_MSEC) {
		size_t nf = (m->alsa_src->num_frames > avail) ? avail :
						m->alsa_src->num_frames;
		alsa_ac_src_read_frames(m->alsa_src, m->alsa_src_sampv, nf);
	}
}


static void fill_play(void)
{
	/* fill play buffer */
	snd_pcm_uframes_t avail;

	while ((avail = snd_pcm_avail(m->alsa_play->write)) >= FRAMES_1_MSEC) {
		size_t nf = (m->alsa_play->num_frames > avail) ? avail :
						m->alsa_play->num_frames;
		alsa_ac_play_write_frames(m->alsa_play,
				m->alsa_play_sampv, nf);
	}
}


static void *module_thread(void *arg)
{
	uint64_t msec = 0;
	uint64_t src_msec = 0;
	uint64_t play_msec = 0;
	bool first_run = true;
	(void)arg;

	info("alsa_audiocore: starting thread\n");

	start_alsa_devices();

	while (m && m->run) {
		/* get MIC and LS data */
		alsa_ac_src_read(m->alsa_src, m->alsa_src_sampv);
		if (first_run)
			empty_src();

		pthread_mutex_lock(&m->mutex);
		if (m->play_started) {
			while (play_msec + m->play->prm.ptime <= msec) {
				m->play->wh(&m->play->af, m->play->arg);
				/* set correct timestamp */
				m->play->af.timestamp = play_msec *
					MSEC_TO_USEC;
				aubuf_write_auframe(m->play_aubuf,
					&m->play->af);
				play_msec += m->play->prm.ptime;
			}
		}
		else
			play_msec = msec;

		aubuf_read_auframe(m->play_aubuf, &m->alsa_play_af);

		/* process bx */
		audiocore_process_bx(m->alsa_play_sampv, m->alsa_play_sampc);
		alsa_ac_play_write(m->alsa_play, m->alsa_play_sampv);
		if (first_run)
			fill_play();

		/* process bz */
		audiocore_process_bz(m->alsa_src_sampv, m->alsa_src_sampc);
		if (m->src_started) {
			/* set correct timestamp */
			m->alsa_src_af.timestamp = msec * MSEC_TO_USEC;
			aubuf_write_auframe(m->src_aubuf, &m->alsa_src_af);

			/* send max. one packet to avoid receiver
			jitter-buffer confusion */
			if (src_msec + m->src->prm.ptime <= msec) {
				aubuf_read_auframe(m->src_aubuf, &m->src->af);
				m->src->rh(&m->src->af, m->src->arg);
				src_msec += m->src->prm.ptime;
			}
		}
		else
			src_msec = msec;
		pthread_mutex_unlock(&m->mutex);

		msec += m->alsa_play_prm.ptime;
		first_run = false;
	}

	return NULL;
}


static void alsa_audiocore_st_destructor(void *arg)
{
	struct alsa_audiocore_st *st = (struct alsa_audiocore_st *)arg;

	info("alsa_audiocore: alsa_audiocore_st_destructor\n");

	if (st->run) {
		debug("alsa_audiocore: stopping thread\n");
		st->run = false;
		(void)pthread_join(st->thread, NULL);
	}

	st->alsa_play = mem_deref(st->alsa_play);
	st->alsa_src = mem_deref(st->alsa_src);
	st->alsa_play_sampv = mem_deref(st->alsa_play_sampv);
	st->alsa_src_sampv = mem_deref(st->alsa_src_sampv);
	st->src_aubuf = mem_deref(st->src_aubuf);
	st->play_aubuf = mem_deref(st->play_aubuf);
	pthread_mutex_destroy(&st->mutex);
}


static void ausrc_destructor(void *arg)
{
	struct ausrc_st *st = arg;

	pthread_mutex_lock(&m->mutex);
	if (m) {
		m->src_started = false;
		m->src = NULL;
		aubuf_flush(m->src_aubuf);
		if (!m->play_started && !m->src_started)
		{
			audiocore_stop();
			audiocore_enable(false);
		}
	}
	pthread_mutex_unlock(&m->mutex);

	mem_deref(st->sampv);

	info("alsa_audiocore: audio player closed\n");
}


static int src_alloc(struct ausrc_st **stp, const struct ausrc *as,
		   struct ausrc_prm *prm, const char *device,
		   ausrc_read_h *rh, ausrc_error_h *errh, void *arg)
{
	struct ausrc_st *st;
	(void)device;
	(void)errh;

	if (!stp || !as || !prm || !rh || !m)
		return EINVAL;

	*stp = NULL;

	if (m->src) {
		warning("alsa_audiocore: src_alloc source already open\n");
		return EBUSY;
	}

	if (prm->srate != m->alsa_src_prm.srate ||
		prm->ch != m->alsa_src_prm.ch ||
		prm->fmt != m->alsa_src_prm.fmt) {
		warning("alsa_audiocore: src_alloc prm mismatch\n");
		return EINVAL;
	}

	if (prm->ptime < m->alsa_src_prm.ptime) {
		warning("alsa_audiocore: baresip TX ptime is less than "
		"ALSA ptime\n");
		return EINVAL;
	}

	st = mem_zalloc(sizeof(*st), ausrc_destructor);
	if (!st)
		return ENOMEM;

	st->prm = *prm;
	st->rh  = rh;
	st->arg = arg;
	st->sampc = prm->srate * prm->ch * prm->ptime / 1000;
	st->sampv = mem_alloc(aufmt_sample_size(prm->fmt) * st->sampc, NULL);
	if (!st->sampv)
		return ENOMEM;

	auframe_init(&st->af, st->prm.fmt, st->sampv, st->sampc, st->prm.srate,
		     st->prm.ch);

	*stp = st;

	pthread_mutex_lock(&m->mutex);
	aubuf_flush(m->src_aubuf);
	audiocore_enable(true);
	audiocore_start();
	m->src = st;
	m->src_started = true;
	pthread_mutex_unlock(&m->mutex);

	info("alsa_audiocore: audio source created\n");

	return 0;
}


static void auplay_destructor(void *arg)
{
	struct auplay_st* st = arg;

	pthread_mutex_lock(&m->mutex);
	if (m) {
		m->play_started = false;
		m->play = NULL;
		aubuf_flush(m->play_aubuf);
		if (!m->play_started && !m->src_started)
		{
			audiocore_stop();
			audiocore_enable(false);
		}
	}
	pthread_mutex_unlock(&m->mutex);

	mem_deref(st->sampv);

	info("alsa_audiocore: audio source closed\n");
}

static int play_alloc(struct auplay_st **stp, const struct auplay *ap,
		    struct auplay_prm *prm, const char *device,
		    auplay_write_h *wh, void *arg)
{
	struct auplay_st *st;
	(void)device;

	if (!stp || !ap || !prm || !wh || !m)
		return EINVAL;

	*stp = NULL;

	if (m->play) {
		warning("alsa_audiocore: play_alloc player already open\n");
		return EBUSY;
	}

	if (prm->srate != m->alsa_play_prm.srate ||
		prm->ch != m->alsa_play_prm.ch ||
		prm->fmt != m->alsa_play_prm.fmt) {
		warning("alsa_audiocore: play_alloc prm mismatch\n");
		return EINVAL;
	}

	st = mem_zalloc(sizeof(*st), auplay_destructor);
	if (!st)
		return ENOMEM;

	st->prm = *prm;
	st->wh  = wh;
	st->arg = arg;
	st->sampc = st->prm.srate * prm->ch * st->prm.ptime / 1000;
	st->sampv = mem_alloc(aufmt_sample_size(prm->fmt) * st->sampc, NULL);
	if (!st->sampv)
		return ENOMEM;

	auframe_init(&st->af, st->prm.fmt, st->sampv, st->sampc, st->prm.srate,
		     st->prm.ch);
	*stp = st;

	pthread_mutex_lock(&m->mutex);
	aubuf_flush(m->play_aubuf);
	audiocore_start();
	m->play = st;
	m->play_started = true;
	pthread_mutex_unlock(&m->mutex);

	info("alsa_audiocore: audio player created\n");

	return 0;
}


static int verify_config(void)
{
	struct config *conf = conf_config();

	if (strcmp(conf->audio.play_mod, alsa_ac_module_name) ||
		strcmp(conf->audio.src_mod, alsa_ac_module_name)) {
		warning("alsa_audiocore: 'audio_source' and 'audio_player' "
		"must be alsa_audiocore\n");
		return EINVAL;
	}

	(void)str_ncpy(m->alsa_play_device, conf->audio.play_dev,
		sizeof(m->alsa_play_device));
	if (!str_isset(m->alsa_play_device))
		(void)str_ncpy(m->alsa_play_device, alsa_ac_default_dev,
			sizeof(m->alsa_play_device));

	(void)str_ncpy(m->alsa_src_device, conf->audio.src_dev,
		sizeof(m->alsa_src_device));
	if (!str_isset(m->alsa_src_device))
		(void)str_ncpy(m->alsa_src_device, alsa_ac_default_dev,
			sizeof(m->alsa_src_device));

	return 0;
}

static int module_init(void)
{
	int err;
	size_t maxsz;
	size_t wishsz;

	info("alsa_audiocore: module_init\n");

	err = audiocore_init();
	if (err)
		goto out;
	audiocore_enable(false);

	err  = ausrc_register(&ausrc, baresip_ausrcl(),
			      alsa_ac_module_name, src_alloc);
	err |= auplay_register(&auplay, baresip_auplayl(),
			       alsa_ac_module_name, play_alloc);
	if (err)
		goto out;

	/* module initialization */
	if (!m)
		m = mem_zalloc(sizeof(*m), alsa_audiocore_st_destructor);

	if (!m) {
		warning("alsa_audiocore: could not allocate audiocore "
				"state\n");
		err = ENOMEM;
		goto out;
	}

	err = verify_config();
	if (err)
		goto out;

	/* ALSA configuration */
	m->alsa_play_prm.srate = m->alsa_src_prm.srate = 16000;
	m->alsa_play_prm.ch = m->alsa_src_prm.ch = 1;
	m->alsa_play_prm.ptime = m->alsa_src_prm.ptime = 8; /* 8 msec */
	m->alsa_play_prm.fmt = m->alsa_src_prm.fmt = AUFMT_S16LE;

	m->alsa_play_sampc = m->alsa_play_prm.srate * m->alsa_play_prm.ch *
		m->alsa_play_prm.ptime / 1000;
	m->alsa_play_num_frames = m->alsa_play_prm.srate *
		m->alsa_play_prm.ptime / 1000;
	m->alsa_play_sampv = mem_alloc(aufmt_sample_size(
		m->alsa_play_prm.fmt) * m->alsa_play_sampc, NULL);
	if (!m->alsa_play_sampv) {
		err = ENOMEM;
		goto out;
	}

	auframe_init(&m->alsa_play_af, m->alsa_play_prm.fmt,
		m->alsa_play_sampv, m->alsa_play_sampc,
		m->alsa_play_prm.srate, m->alsa_play_prm.ch);

	m->alsa_src_sampc = m->alsa_src_prm.srate * m->alsa_src_prm.ch *
		m->alsa_src_prm.ptime / 1000;
	m->alsa_src_num_frames = m->alsa_src_prm.srate *
		m->alsa_src_prm.ptime / 1000;
	m->alsa_src_sampv = mem_alloc(aufmt_sample_size(
		m->alsa_src_prm.fmt) * m->alsa_src_sampc, NULL);
	if (!m->alsa_src_sampv) {
		err = ENOMEM;
		goto out;
	}

	auframe_init(&m->alsa_src_af, m->alsa_src_prm.fmt,
		m->alsa_src_sampv, m->alsa_src_sampc,
		m->alsa_src_prm.srate, m->alsa_src_prm.ch);

	err |= alsa_ac_src_alloc(&m->alsa_src,
		&m->alsa_src_prm, m->alsa_src_device);
	err |= alsa_ac_play_alloc(&m->alsa_play,
		&m->alsa_play_prm, m->alsa_play_device);
	if (err)
		goto out;

	/* TODO calculate best size */
	wishsz = aufmt_sample_size(m->alsa_src_prm.fmt) *
		m->alsa_src_sampc * 3;
	maxsz = 0;
	err = aubuf_alloc(&m->src_aubuf, wishsz, maxsz);
	if (err) {
		warning("alsa_audiocore: Could not allocate src aubuf."
				" wishsz=%lu, maxsz=%lu (%m)\n", wishsz,
				maxsz, err);
		goto out;
	}
	aubuf_set_mode(m->src_aubuf, AUBUF_FIXED);

	/* TODO calculate best size */
	wishsz = aufmt_sample_size(m->alsa_play_prm.fmt) *
		m->alsa_play_sampc * 3;
	maxsz = 0;
	err = aubuf_alloc(&m->play_aubuf, wishsz, maxsz);
	if (err) {
		warning("alsa_audiocore: Could not allocate play aubuf. "
				"wishsz=%lu, maxsz=%lu (%m)\n", wishsz,
				maxsz, err);
		goto out;
	}
	aubuf_set_mode(m->play_aubuf, AUBUF_FIXED);

	err = pthread_mutex_init(&m->mutex, NULL);
	if (err)
		goto out;

	m->run = true;
	{
		pthread_attr_t tattr;
		int newprio = 20;
		struct sched_param param;

		/* initialized with default attributes */
		pthread_attr_init(&tattr);
		pthread_attr_setschedpolicy(&tattr, SCHED_RR);
		pthread_attr_getschedparam(&tattr, &param);
		param.sched_priority = newprio;
		pthread_attr_setschedparam(&tattr, &param);

		err = pthread_create(&m->thread, &tattr, module_thread, NULL);
	}
	if (err) {
		m->run = false;
		goto out;
	}

out:
	if (err) {
		audiocore_close();
		m = mem_deref(m);
	}
	return err;
}


static int module_close(void)
{
	info("alsa_audiocore: module_close\n");
	ausrc  = mem_deref(ausrc);
	auplay = mem_deref(auplay);

	m = mem_deref(m);

	audiocore_close();

	/* releases all resources of the global configuration tree,
	   and sets snd_config to NULL. */
	snd_config_update_free_global();

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(alsa_audiocore) = {
	alsa_ac_module_name,
	"sound",
	module_init,
	module_close
};
