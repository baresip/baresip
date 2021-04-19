/**
 * @file multicast/player.c
 *
 * Copyright (C) 2021 Commend.com - c.huber@commend.com
 */

#include <re.h>
#include <rem.h>
#include <baresip.h>
#ifdef HAVE_PTHREAD
#include <pthread.h>
#endif


#include "multicast.h"


#define DEBUG_MODULE "mcplayer"
#define DEBUG_LEVEL 6
#include <re_dbg.h>


/**
 * Multicast player struct
 *
 * Contains configuration of the audio player and buffer for the audio data
 */
struct mcplayer{
	struct config_audio *cfg;
	struct jbuf *jbuf;
	bool jbuf_started;

	struct auplay_st *auplay;
	struct auplay_prm auplay_prm;
	const struct aucodec *ac;
	struct audec_state *dec;
	struct aubuf *aubuf;
	volatile bool aubuf_started;
	size_t aubuf_minsz;
	size_t aubuf_maxsz;
	size_t num_bytes;

	struct auresamp resamp;
	struct list filterl;
	char *module;
	char *device;
	void*sampv;
	int16_t *sampv_rs;
	uint32_t ptime;
	enum aufmt play_fmt;
	enum aufmt dec_fmt;
	uint32_t again;

#ifdef HAVE_PTHREAD
	struct {
		pthread_t tid;
		bool run;
		pthread_cond_t cond;
		pthread_mutex_t mutex;
	} thr;
#else
	struct tmr tmr;
#endif
};


static struct mcplayer *player;


static void mcplayer_destructor(void *arg)
{
	(void) arg;

	player->auplay = mem_deref(player->auplay);

#ifdef HAVE_PTHREAD
	if (player->thr.run) {
		player->thr.run = false;
		pthread_join(player->thr.tid, NULL);
	}

	pthread_mutex_destroy(&player->thr.mutex);
	pthread_cond_destroy(&player->thr.cond);
#else
	tmr_cancel(&player->tmr);
#endif

	player->jbuf     = mem_deref(player->jbuf);
	player->module   = mem_deref(player->module);
	player->device   = mem_deref(player->device);
	player->dec      = mem_deref(player->dec);

	player->sampv    = mem_deref(player->sampv);
	player->sampv_rs = mem_deref(player->sampv_rs);
	player->aubuf    = mem_deref(player->aubuf);
}


/**
 * Decode the payload of the RTP packet
 *
 * @param hdr RTP header
 * @param mb  RTP payload
 *
 * @return 0 if success, otherwise errorcode
 */
static int stream_recv_handler(const struct rtp_header *hdr, struct mbuf *mb)
{
	struct auframe af;
	struct le *le;
	size_t sampc = AUDIO_SAMPSZ;
	bool marker = hdr->m;
	void *sampv;
	int err = 0;

	if (!player)
		return EINVAL;

	if (!player->ac)
		return 0;

	if (hdr->ext && hdr->x.len && mb)
		return ENOTSUP;

	if (mbuf_get_left(mb)) {
		err = player->ac->dech(player->dec, player->dec_fmt,
			player->sampv, &sampc, marker,
			mbuf_buf(mb), mbuf_get_left(mb));
		if (err)
			goto out;
	}
	else if (player->ac->plch && player->dec_fmt == AUFMT_S16LE) {
		err = player->ac->plch(player->dec, player->dec_fmt,
			player->sampv, &sampc,
			mbuf_buf(mb), mbuf_get_left(mb));
		if (err)
			goto out;
	}
	else {
		sampc = 0;
	}

	auframe_init(&af, player->dec_fmt, player->sampv, sampc);

	for (le = player->filterl.tail; le; le = le->prev) {
		struct aufilt_dec_st *st = le->data;

		if (st->af && st->af->dech)
			err |= st->af->dech(st, &af);

	}

	if (!player->aubuf)
		goto out;

	sampv = af.sampv;
	sampc = af.sampc;

	if (player->resamp.resample) {
		size_t sampc_rs = AUDIO_SAMPSZ;

		if (player->dec_fmt != AUFMT_S16LE)
			return ENOTSUP;

		err = auresamp(&player->resamp, player->sampv_rs, &sampc_rs,
			player->sampv, sampc);
		if (err)
			goto out;

		sampv = player->sampv_rs;
		sampc = sampc_rs;
	}

	if (player->play_fmt == player->dec_fmt) {
		size_t num_bytes = sampc * aufmt_sample_size(player->play_fmt);
		err = aubuf_write(player->aubuf, sampv, num_bytes);
		if (err)
			goto out;
	}
	else if (player->dec_fmt == AUFMT_S16LE) {
		void *tmp_sampv;

		size_t num_bytes = sampc * aufmt_sample_size(player->play_fmt);

		tmp_sampv = mem_zalloc(num_bytes, NULL);
		if (!tmp_sampv)
			return ENOMEM;

		auconv_from_s16(player->play_fmt, tmp_sampv, sampv, sampc);
		err = aubuf_write(player->aubuf, tmp_sampv, num_bytes);
		mem_deref(tmp_sampv);

		if (err)
			goto out;
	}
	else {
		warning ("multicast player: invalid sample format "
			"(%s -> %s)\n", aufmt_name(player->dec_fmt),
			aufmt_name(player->play_fmt));
	}

	player->aubuf_started = true;

  out:

	return err;
}


/**
 * Decode RTP packet
 *
 * @param arg Multicast player object
 *
 * @return 0 if success, otherwise errorcode
 */
static int stream_decode(void *arg)
{
	void *mb = NULL;
	struct rtp_header hdr;
	int err = 0;

	(void) arg;

	if (!player)
		return EINVAL;

	if (!player->jbuf)
		return ENOENT;

	err = jbuf_get(player->jbuf, &hdr, &mb);
	if (err && err != EAGAIN)
		return ENOENT;

	player->jbuf_started = true;

	err = stream_recv_handler(&hdr, mb);
	mb = mem_deref(mb);

	return err;
}


/**
 * Decode audio
 *
 * @param arg Multicast player object
 */
static void audio_decode(void *arg)
{
	int err = 0;

	(void) arg;

	while (err == EAGAIN ||
		(!err && aubuf_cur_size(player->aubuf) < player->num_bytes)) {
		if (err == EAGAIN)
			player->again++;

		err = stream_decode(player);
		if (err && err != EAGAIN)
			break;

#ifdef HAVE_PTHREAD
		if (!player->thr.run)
			break;
#endif
	}
	return;
}


#ifdef HAVE_PTHREAD
/**
 * Receiver Thread, which decodecs the stream contained in the jbuf
 *
 * @param arg Multicast player object
 *
 * @return NULL
 */
static void *rx_thread(void *arg)
{
	struct timespec ts;
	uint64_t ms;
	int err = 0;

	(void) arg;

	while (player->thr.run) {
		ms = tmr_jiffies() + 500;
		ts.tv_sec = ms / 1000;
		ts.tv_nsec = (ms % 1000) * 1000000UL;

		err = pthread_mutex_lock(&player->thr.mutex);
		if (err)
			return NULL;

		pthread_cond_timedwait(&player->thr.cond,
			&player->thr.mutex, &ts);

		err = pthread_mutex_unlock(&player->thr.mutex);
		if (!player->thr.run || err)
			break;

		audio_decode(player);
	}

	return NULL;
}
#endif


/**
 * Audio player write handler
 *
 * @param sampv Sample buffer
 * @param sampc Sample counter
 * @param arg   Multicast player object (unused)
 */
static void auplay_write_handler(struct auframe *af, void *arg)
{
	(void) arg;

	if (!player)
		return;

	player->num_bytes = af->sampc * aufmt_sample_size(player->play_fmt);

	aubuf_read(player->aubuf, af->sampv, player->num_bytes);

#ifdef HAVE_PTHREAD
	pthread_mutex_lock(&player->thr.mutex);
	if (!player->thr.run) {
		int err;

		player->thr.run = true;
		err = pthread_create(&player->thr.tid, NULL,
			rx_thread, player);
		if (err) {
			player->thr.run = false;
			return;
		}
	}

	pthread_cond_signal(&player->thr.cond);
	pthread_mutex_unlock(&player->thr.mutex);
#else
	tmr_start(&player->tmr, 0, audio_decode, player);
#endif
}


/**
 * Setup all available audio filter for the decoder
 *
 * @param aufiltl List of audio filter
 *
 * @return 0 if success, otherwise errorcode
 */
static int aufilt_setup(struct list *aufiltl)
{
	struct aufilt_prm prm;
	struct le *le;
	int err = 0;

	if (!player->ac)
		return 0;

	if (!list_isempty(&player->filterl))
		return 0;

	prm.srate = player->ac->srate;
	prm.ch = player->ac->ch;
	prm.fmt = player->dec_fmt;

	for (le = list_head(aufiltl); le; le = le->next) {
		struct aufilt *af = le->data;
		struct aufilt_dec_st *decst = NULL;
		void *ctx = NULL;

		if (af->decupdh) {
			err = af->decupdh(&decst, &ctx, af, &prm, NULL);
			if (err) {
				warning("multicast player: error in decoder"
					"autio-filter '%s' (%m)\n",
					af->name, err);
			}
			else {
				decst->af = af;
				list_append(&player->filterl, &decst->le,
					decst);
			}
		}

		if (err) {
			warning("multicast player: audio-filter '%s' "
				"update failed (%m)\n", af->name, err);
			break;
		}
	}

	return err;
}


/**
 * Allocate and start a media player for the multicast
 *
 * @note singleton
 *
 * @param jbuf Jitter buffer containing the RTP stream
 * @param ac   Audio codec
 *
 * @return 0 if success, otherwise errorcode
 */
int mcplayer_start(struct jbuf *jbuf, const struct aucodec *ac)
{
	int err = 0;
	struct config_audio *cfg = &conf_config()->audio;
	uint32_t srate_dsp;
	uint32_t channels_dsp;
	struct auplay_prm prm;
	bool resamp = false;

	if (!jbuf || !ac)
		return EINVAL;

	if (player) {
		warning ("multicast player: already started\n");
		return EINPROGRESS;
	}

	player = mem_zalloc(sizeof(*player), mcplayer_destructor);
	if (!player)
		return ENOMEM;

	player->cfg = cfg;
	player->ac = ac;
	player->jbuf = mem_ref(jbuf);
	player->play_fmt = cfg->play_fmt;
	player->dec_fmt = cfg->dec_fmt;

	err = str_dup(&player->module, cfg->play_mod);
	err |= str_dup(&player->device, cfg->play_dev);
	if (err)
		goto out;

	player->sampv = mem_zalloc(AUDIO_SAMPSZ *
		aufmt_sample_size(player->dec_fmt), NULL);
	if (!player->sampv) {
		err = ENOMEM;
		goto out;
	}

	auresamp_init(&player->resamp);
	player->ptime = PTIME;

#ifdef HAVE_PTHREAD
	err = pthread_mutex_init(&player->thr.mutex, NULL);
	err |= pthread_cond_init(&player->thr.cond, NULL);
	if (err)
		goto out;
#endif

	if (player->ac->decupdh) {
		err = player->ac->decupdh(&player->dec, player->ac, NULL);
		if (err) {
			warning ("multicast player: alloc decoder(%m)\n",
				err);
			goto out;
		}
	}

	srate_dsp = player->ac->srate;
	channels_dsp = player->ac->ch;
	if (cfg->srate_play && cfg->srate_play != srate_dsp) {
		resamp = true;
		srate_dsp = cfg->srate_play;
	}
	if (cfg->channels_play && cfg->channels_play != channels_dsp) {
		resamp = true;
		channels_dsp = cfg->channels_play;
	}

	if (resamp && !player->sampv_rs) {
		player->sampv_rs = mem_zalloc(AUDIO_SAMPSZ * sizeof(int16_t),
			NULL);

		if (!player->sampv_rs) {
			err = ENOMEM;
			goto out;
		}

		err = auresamp_setup(&player->resamp,
			player->ac->srate, player->ac->ch,
			srate_dsp, channels_dsp);
		if (err) {
			warning("multicast player: could not setup auplay"
				" resampler (%m)\n", err);
			goto out;
		}
	}

	prm.srate = srate_dsp;
	prm.ch = channels_dsp;
	prm.ptime = player->ptime;
	prm.fmt = player->play_fmt;
	if (!player->aubuf) {
		const size_t sz = aufmt_sample_size(player->play_fmt);
		const size_t ptime_min = cfg->buffer.min;
		const size_t ptime_max = cfg->buffer.max;
		size_t min_sz;
		size_t max_sz;

		if (!ptime_min || !ptime_max) {
			err = EINVAL;
			goto out;
		}

		min_sz = sz * ((prm.srate * prm.ch * ptime_min) / 10000);
		max_sz = sz * ((prm.srate * prm.ch * ptime_max) / 10000);

		err = aubuf_alloc(&player->aubuf, min_sz, max_sz * 2);
		if (err) {
			warning("multicast player: aubuf alloc error (%m)\n",
				err);
			goto out;
		}
	}

	err = aufilt_setup(baresip_aufiltl());
	if (err)
	{
		warning("multicast player: aufilt setup error (%m)\n)", err);
		goto out;
	}

	err = auplay_alloc(&player->auplay, baresip_auplayl(), player->module,
		&prm, player->device, auplay_write_handler, player);
	if (err) {
		warning("multicast player: start of %s.%s failed (%m)\n",
			player->module, player->device, err);
		goto out;
	}

	player->auplay_prm = prm;

  out:
	if (err)
		player = mem_deref(player);

	return err;
}


/**
 * Stop multicast player
 */
void mcplayer_stop(void)
{
	player = mem_deref(player);
}
