/**
 * @file multicast/player.c
 *
 * Copyright (C) 2021 Commend.com - c.huber@commend.com
 */

#include <re.h>
#include <rem.h>
#include <baresip.h>

#include "multicast.h"


#define DEBUG_MODULE "mcplayer"
#define DEBUG_LEVEL 6
#include <re_dbg.h>


enum fade_state {
	FM_IDLE,
	FM_FADEIN,
	FM_FADEINDONE,
	FM_FADEOUT,
	FM_FADEOUTDONE,
};


/**
 * Multicast player struct
 *
 * Contains configuration of the audio player and buffer for the audio data
 */
struct mcplayer {
	struct config_audio *cfg;

	struct auplay_st *auplay;
	struct auplay_prm auplay_prm;
	const struct aucodec *ac;
	struct audec_state *dec;
	struct aubuf *aubuf;
	uint32_t ssrc;

	struct list filterl;
	char *module;
	char *device;
	void *sampv;
	uint32_t ptime;
	enum aufmt play_fmt;
	enum aufmt dec_fmt;

	enum fade_state fades;
	uint32_t fade_cmax;
	uint32_t fade_c;
	float fade_dbstart;
	float fade_delta;
};


static struct mcplayer *player;


static void mcplayer_destructor(void *arg)
{
	(void) arg;

	mem_deref(player->auplay);

	mem_deref(player->module);
	mem_deref(player->device);
	mem_deref(player->dec);

	mem_deref(player->sampv);
	mem_deref(player->aubuf);
	list_flush(&player->filterl);
}


static void fade_process(struct auframe *af)
{
	size_t i;
	int16_t *sampv_ptr = af->sampv;
	float db_value;

	if (af->fmt != AUFMT_S16LE)
		return;

	switch (player->fades) {
		case FM_FADEIN:
			if (player->fade_c == player->fade_cmax) {
				player->fades = FM_FADEINDONE;
				return;
			}

			for (i = 0; i < af->sampc; i++) {
				db_value = player->fade_dbstart +
					(player->fade_c * player->fade_delta);
				*(sampv_ptr) = *(sampv_ptr) * db_value;
				++sampv_ptr;
				if (player->fade_c < player->fade_cmax)
					++player->fade_c;
			}

			break;

		case FM_FADEOUT:
			for (i = 0; i < af->sampc; i++) {
				db_value = player->fade_dbstart +
					(player->fade_c * player->fade_delta);
				*(sampv_ptr) = *(sampv_ptr) * db_value;
				++sampv_ptr;

				if (player->fade_c > 0)
					--player->fade_c;
			}

			if (!player->fade_c) {
				player->fades = FM_FADEOUTDONE;
				return;
			}

			break;

		case FM_FADEOUTDONE:
			for (i = 0; i < af->sampc; i++) {
				db_value = 1. - ((player->fade_cmax - 1) *
					player->fade_delta);
				*(sampv_ptr) = *(sampv_ptr) * db_value;
				++sampv_ptr;
			}

			break;

		default:
			break;

	}

	return;
}


/**
 * Decode the payload of the RTP packet
 *
 * @param hdr   RTP header
 * @param mb    RTP payload
 * @param drop  True if the jbuf returned EAGAIN
 *
 * @return 0 if success, otherwise errorcode
 */
int mcplayer_decode(const struct rtp_header *hdr, struct mbuf *mb, bool drop)
{
	struct auframe af;
	struct le *le;
	size_t sampc = AUDIO_SAMPSZ;
	bool marker = hdr->m;
	int err = 0;

	if (!player)
		return EINVAL;

	if (!player->ac)
		return 0;

	if (hdr->ext && hdr->x.len && mb)
		return ENOTSUP;

	if (player->ssrc != hdr->ssrc)
		aubuf_flush(player->aubuf);

	player->ssrc = hdr->ssrc;
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
		/* no PLC in the codec, might be done in filters below */
		sampc = 0;
	}

	auframe_init(&af, player->dec_fmt, player->sampv, sampc,
		     player->ac->srate, player->ac->ch);
	af.timestamp = ((uint64_t) hdr->ts) * AUDIO_TIMEBASE /
		       player->ac->crate;

	for (le = player->filterl.tail; le; le = le->prev) {
		struct aufilt_dec_st *st = le->data;

		if (st->af && st->af->dech)
			err |= st->af->dech(st, &af);

	}

	if (!player->aubuf)
		goto out;

	if (af.fmt != player->play_fmt) {
		warning("multicast player: invalid sample formats (%s -> %s)."
			" %s\n",
			aufmt_name(af.fmt), aufmt_name(player->play_fmt),
			player->play_fmt == AUFMT_S16LE ?
			"Use module auconv!" : "");
	}

	if (player->auplay_prm.srate != af.srate ||
	    player->auplay_prm.ch != af.ch) {
		warning("multicast: srate/ch of frame %u/%u vs "
			"player %u/%u. Use module auresamp!\n",
			af.srate, af.ch,
			player->auplay_prm.srate, player->auplay_prm.ch);
	}

	if (drop) {
		aubuf_drop_auframe(player->aubuf, &af);
		goto out;
	}

	fade_process(&af);
	err = aubuf_write_auframe(player->aubuf, &af);

  out:

	return err;
}


/**
 * Audio player write handler
 *
 * @param af   Audio frame (af.sampv, af.sampc and af.fmt needed)
 * @param arg  unused
 */
static void auplay_write_handler(struct auframe *af, void *arg)
{
	(void) arg;

	if (!player)
		return;

	aubuf_read_auframe(player->aubuf, af);
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
 * @param ac   Audio codec
 *
 * @return 0 if success, otherwise errorcode
 */
int mcplayer_start(const struct aucodec *ac)
{
	int err = 0;
	struct config_audio *cfg = &conf_config()->audio;
	uint32_t srate_dsp;
	uint32_t channels_dsp;
	struct auplay_prm prm;

	if (!ac)
		return EINVAL;

	if (player &&
		(player->fades == FM_FADEOUT || player->fades == FM_FADEIN))
		return EINPROGRESS;

	player = mem_deref(player);
	player = mem_zalloc(sizeof(*player), mcplayer_destructor);
	if (!player)
		return ENOMEM;

	player->cfg = cfg;
	player->ac = ac;
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

	player->ptime = PTIME;
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

	prm.srate = srate_dsp;
	prm.ch = channels_dsp;
	prm.ptime = player->ptime;
	prm.fmt = player->play_fmt;

	if (multicast_fade_time()) {
		player->fade_cmax = (multicast_fade_time() * prm.srate) / 1000;
		player->fade_dbstart = 0.001; /*-60dB*/
		player->fade_delta = (1. - player->fade_dbstart) /
			player->fade_cmax;
		player->fades = FM_FADEIN;
	}

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

		min_sz = sz * calc_nsamp(prm.srate, prm.ch, ptime_min);
		max_sz = sz * calc_nsamp(prm.srate, prm.ch, ptime_max);

		err = aubuf_alloc(&player->aubuf, min_sz, max_sz);
		if (err) {
			warning("multicast player: aubuf alloc error (%m)\n",
				err);
			goto out;
		}

		aubuf_set_mode(player->aubuf, cfg->adaptive ?
			       AUBUF_ADAPTIVE : AUBUF_FIXED);
		aubuf_set_silence(player->aubuf, cfg->silence);
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


/**
 * Fade-out active player
 *
 */
void mcplayer_fadeout(void)
{
	if (!player)
		return;

	if (player->fades == FM_FADEOUT || player->fades == FM_FADEOUTDONE)
		return;

	player->fades = FM_FADEOUT;
}


/**
 * @return True if the fade-out finished
 */
bool mcplayer_fadeout_done(void)
{
	if (!player)
		return false;

	return player->fades == FM_FADEOUTDONE;
}


/**
 * Fade-in active player
 *
 * @param restart  If true the fade-in restarts with silence level
 */
void mcplayer_fadein(bool restart)
{
	if (!player)
		return;

	if (restart)
		player->fade_c = 0;
	else if (player->fades == FM_FADEINDONE)
		return;

	player->fades = FM_FADEIN;
}

/**
 * Initialize everything needed for the player beforhand
 *
 * @return 0 if success, otherwise errorcode
 */
int mcplayer_init(void)
{
	return 0;
}

/**
 * Terminate everything needed for the player afterwards
 *
 */
void mcplayer_terminate(void)
{
	return;
}
