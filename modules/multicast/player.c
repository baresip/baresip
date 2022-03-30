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


/**
 * Multicast player struct
 *
 * Contains configuration of the audio player and buffer for the audio data
 */
struct mcplayer{
	struct config_audio *cfg;
	struct jbuf *jbuf;

	struct auplay_st *auplay;
	struct auplay_prm auplay_prm;
	const struct aucodec *ac;
	struct audec_state *dec;
	struct aubuf *aubuf;
	size_t aubuf_minsz;
	size_t aubuf_maxsz;
	size_t num_bytes;

	struct list filterl;
	char *module;
	char *device;
	void *sampv;
	uint32_t ptime;
	enum aufmt play_fmt;
	enum aufmt dec_fmt;
	uint32_t again;
};


static struct mcplayer *player;


static void mcplayer_destructor(void *arg)
{
	(void) arg;

	mem_deref(player->auplay);

	mem_deref(player->jbuf);
	mem_deref(player->module);
	mem_deref(player->device);
	mem_deref(player->dec);

	mem_deref(player->sampv);
	mem_deref(player->aubuf);
	list_flush(&player->filterl);
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
		/* no PLC in the codec, might be done in filters below */
		sampc = 0;
	}

	auframe_init(&af, player->dec_fmt, player->sampv, sampc,
		     player->auplay_prm.srate, player->auplay_prm.ch);
	af.timestamp = hdr->ts;

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

	err = aubuf_write_auframe(player->aubuf, &af);

  out:

	return err;
}


/**
 * Decode RTP packet
 *
 * @return 0 if success, otherwise errorcode
 */
int mcplayer_decode(void)
{
	void *mb = NULL;
	struct rtp_header hdr;
	int err = 0;

	if (!player)
		return EINVAL;

	if (!player->jbuf)
		return ENOENT;

	err = jbuf_get(player->jbuf, &hdr, &mb);
	if (err && err != EAGAIN)
		return err;

	err = stream_recv_handler(&hdr, mb);
	mb = mem_deref(mb);

	return err;
}


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

	player->num_bytes = auframe_size(af);

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
