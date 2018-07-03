/**
 * @file src/play.c  Audio-file player
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <stdlib.h>
#include <string.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "core.h"


enum {PTIME = 40};

/** Audio file player */
struct play {
	struct le le;
	struct play **playp;
	struct lock *lock;
	struct mbuf *mb;
	struct auplay_st *auplay;
	struct tmr tmr;
	int repeat;
	bool eof;
};


#ifndef PREFIX
#define PREFIX "/usr"
#endif
static const char default_play_path[FS_PATH_MAX] = PREFIX "/share/baresip";


struct player {
	struct list playl;
	char play_path[FS_PATH_MAX];
};


static void tmr_polling(void *arg);


static void tmr_stop(void *arg)
{
	struct play *play = arg;
	debug("play: player complete.\n");
	mem_deref(play);
}


static void tmr_polling(void *arg)
{
	struct play *play = arg;

	lock_write_get(play->lock);

	tmr_start(&play->tmr, 1000, tmr_polling, arg);

	if (play->eof) {
		if (play->repeat == 0)
			tmr_start(&play->tmr, 1, tmr_stop, arg);
	}

	lock_rel(play->lock);
}


/*
 * NOTE: DSP cannot be destroyed inside handler
 */
static void write_handler(void *sampv, size_t sampc, void *arg)
{
	struct play *play = arg;
	size_t sz = sampc * 2;
	size_t pos = 0;
	size_t left;
	size_t count;

	lock_write_get(play->lock);

	if (play->eof)
		goto silence;

	while (pos < sz) {
		left = mbuf_get_left(play->mb);
		count = (left > sz - pos) ? sz - pos : left;

		(void)mbuf_read_mem(play->mb, (uint8_t *)sampv + pos, count);

		pos += count;

		if (pos < sz) {
			if (play->repeat > 0)
				play->repeat--;

			if (play->repeat == 0) {
				play->eof = true;
				goto silence;
			}

			play->mb->pos = 0;
		}
	}

 silence:
	if (play->eof)
		memset((uint8_t *)sampv + pos, 0, sz - pos);

	lock_rel(play->lock);
}


static void destructor(void *arg)
{
	struct play *play = arg;

	list_unlink(&play->le);
	tmr_cancel(&play->tmr);

	lock_write_get(play->lock);
	play->eof = true;
	lock_rel(play->lock);

	mem_deref(play->auplay);
	mem_deref(play->mb);
	mem_deref(play->lock);

	if (play->playp)
		*play->playp = NULL;
}


static int aufile_load(struct mbuf *mb, const char *filename,
		       uint32_t *srate, uint8_t *channels)
{
	struct aufile_prm prm;
	struct aufile *af;
	int err;

	err = aufile_open(&af, &prm, filename, AUFILE_READ);
	if (err)
		return err;

	while (!err) {
		uint8_t buf[4096];
		size_t i, n;
		int16_t *p = (void *)buf;

		n = sizeof(buf);

		err = aufile_read(af, buf, &n);
		if (err || !n)
			break;

		switch (prm.fmt) {

		case AUFMT_S16LE:
			/* convert from Little-Endian to Native-Endian */
			for (i=0; i<n/2; i++) {
				int16_t s = sys_ltohs(*p++);
				err |= mbuf_write_u16(mb, s);
			}

			break;

		case AUFMT_PCMA:
			for (i=0; i<n; i++) {
				err |= mbuf_write_u16(mb,
						      g711_alaw2pcm(buf[i]));
			}
			break;

		case AUFMT_PCMU:
			for (i=0; i<n; i++) {
				err |= mbuf_write_u16(mb,
						      g711_ulaw2pcm(buf[i]));
			}
			break;

		default:
			err = ENOSYS;
			break;
		}
	}

	mem_deref(af);

	if (!err) {
		mb->pos = 0;

		*srate    = prm.srate;
		*channels = prm.channels;
	}

	return err;
}


/**
 * Play a tone from a PCM buffer
 *
 * @param playp    Pointer to allocated player object
 * @param player   Audio-file player
 * @param tone     PCM buffer to play
 * @param srate    Sampling rate
 * @param ch       Number of channels
 * @param repeat   Number of times to repeat
 *
 * @return 0 if success, otherwise errorcode
 */
int play_tone(struct play **playp, struct player *player,
	      struct mbuf *tone, uint32_t srate,
	      uint8_t ch, int repeat)
{
	struct auplay_prm wprm;
	struct play *play;
	struct config *cfg;
	int err;

	if (!player)
		return EINVAL;
	if (playp && *playp)
		return EALREADY;

	cfg = conf_config();
	if (!cfg)
		return ENOENT;

	play = mem_zalloc(sizeof(*play), destructor);
	if (!play)
		return ENOMEM;

	tmr_init(&play->tmr);
	play->repeat = repeat;
	play->mb     = mem_ref(tone);

	err = lock_alloc(&play->lock);
	if (err)
		goto out;

	wprm.ch         = ch;
	wprm.srate      = srate;
	wprm.ptime      = PTIME;
	wprm.fmt        = AUFMT_S16LE;

	err = auplay_alloc(&play->auplay, baresip_auplayl(),
			   cfg->audio.alert_mod, &wprm,
			   cfg->audio.alert_dev, write_handler, play);
	if (err)
		goto out;

	list_append(&player->playl, &play->le, play);
	tmr_start(&play->tmr, 1000, tmr_polling, play);

 out:
	if (err) {
		mem_deref(play);
	}
	else if (playp) {
		play->playp = playp;
		*playp = play;
	}

	return err;
}


/**
 * Play an audio file in WAV format
 *
 * @param playp    Pointer to allocated player object
 * @param player   Audio-file player
 * @param filename Name of WAV file to play
 * @param repeat   Number of times to repeat
 *
 * @return 0 if success, otherwise errorcode
 */
int play_file(struct play **playp, struct player *player,
	      const char *filename, int repeat)
{
	struct mbuf *mb;
	char path[FS_PATH_MAX];
	uint32_t srate = 0;
	uint8_t ch = 0;
	int err;

	if (!player)
		return EINVAL;
	if (playp && *playp)
		return EALREADY;

	if (re_snprintf(path, sizeof(path), "%s/%s",
			player->play_path, filename) < 0)
		return ENOMEM;

	mb = mbuf_alloc(1024);
	if (!mb)
		return ENOMEM;

	err = aufile_load(mb, path, &srate, &ch);
	if (err) {
		warning("play: %s: %m\n", path, err);
		goto out;
	}

	err = play_tone(playp, player, mb, srate, ch, repeat);

 out:
	mem_deref(mb);

	return err;
}


static void player_destructor(void *data)
{
	struct player *player = data;

	list_flush(&player->playl);
}


int play_init(struct player **playerp)
{
	struct player *player;

	if (!playerp)
		return EINVAL;

	player = mem_zalloc(sizeof(*player), player_destructor);
	if (!player)
		return ENOMEM;

	list_init(&player->playl);

	str_ncpy(player->play_path, default_play_path,
		 sizeof(player->play_path));

	*playerp = player;

	return 0;
}


void play_set_path(struct player *player, const char *path)
{
	if (!player)
		return;

	str_ncpy(player->play_path, path, sizeof(player->play_path));
}
