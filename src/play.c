/**
 * @file src/play.c  Audio-file player
 *
 * Copyright (C) 2010 Alfred E. Heggestad
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
	mtx_t lock;
	struct mbuf *mb;
	struct auplay_st *auplay;
	char *mod;
	char *dev;
	struct tmr tmr;
	int repeat;
	uint64_t delay;
	uint64_t trep;
	bool eof;

	char *filename;
	const struct ausrc *ausrc;
	struct ausrc_st *ausrc_st;
	struct ausrc_prm sprm;
	struct aubuf *aubuf;
	play_finish_h *fh;
	void *arg;
};


#ifndef PREFIX
#define PREFIX "/usr"
#endif


struct player {
	struct list playl;
	char play_path[FS_PATH_MAX];
};


static int start_ausrc(struct play *play);
static int start_auplay(struct play *play);


static void tmr_stop(void *arg)
{
	struct play *play = arg;
	debug("play: player complete.\n");
	mem_deref(play);
}


static void tmr_polling(void *arg)
{
	struct play *play = arg;
	int err = 0;

	mtx_lock(&play->lock);

	tmr_start(&play->tmr, PTIME, tmr_polling, play);

	if (play->eof) {
		if (play->repeat == 0)
			tmr_start(&play->tmr, 1, tmr_stop, arg);
	}
	else if (play->aubuf && !play->auplay) {
		err = start_auplay(play);

		tmr_start(&play->tmr, 4, tmr_polling, play);
	}

	if (play->ausrc && play->trep && play->trep <= tmr_jiffies()) {
		play->trep = 0;
		aubuf_flush(play->aubuf);
		err = start_ausrc(play);
	}

	mtx_unlock(&play->lock);

	if (err)
		tmr_start(&play->tmr, 1, tmr_stop, arg);
}


static bool check_restart(void *arg)
{
	struct play *play = arg;

	if (play->trep) {
		if (play->trep > tmr_jiffies())
			return false;

		play->trep = 0;
		return true;
	}

	if (play->repeat > 0)
		play->repeat--;

	if (play->repeat == 0)
		play->eof = true;
	else
		play->trep = tmr_jiffies() + play->delay;

	return false;
}


/*
 * NOTE: DSP cannot be destroyed inside handler
 */
static void write_handler(struct auframe *af, void *arg)
{
	struct play *play = arg;
	size_t sz = af->sampc * 2;
	size_t pos = 0;
	size_t left;
	size_t count;

	mtx_lock(&play->lock);

	if (play->eof)
		goto silence;

	while (pos < sz) {
		left = mbuf_get_left(play->mb);
		count = (left > sz - pos) ? sz - pos : left;

		(void)mbuf_read_mem(play->mb, (uint8_t *)af->sampv + pos,
				    count);

		pos += count;

		if (pos < sz) {
			if (!check_restart(play))
				goto silence;

			play->mb->pos = 0;
		}
	}

 silence:
	if (play->eof)
		memset((uint8_t *)af->sampv + pos, 0, sz - pos);

	mtx_unlock(&play->lock);
}


static void destructor(void *arg)
{
	struct play *play = arg;

	list_unlink(&play->le);
	tmr_cancel(&play->tmr);

	mtx_lock(&play->lock);
	play->eof = true;
	mtx_unlock(&play->lock);

	mem_deref(play->ausrc_st);
	mem_deref(play->auplay);
	mem_deref(play->mod);
	mem_deref(play->dev);
	mem_deref(play->mb);
	mtx_destroy(&play->lock);
	mem_deref(play->aubuf);
	mem_deref(play->filename);

	if (play->playp)
		*play->playp = NULL;

	if (play->fh)
		play->fh(play, play->arg);
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
 * @param play_mod Audio player module
 * @param play_dev Audio player device
 *
 * @return 0 if success, otherwise errorcode
 */
int play_tone(struct play **playp, struct player *player,
	      struct mbuf *tone, uint32_t srate,
	      uint8_t ch, int repeat,
	      const char *play_mod, const char *play_dev)
{
	struct auplay_prm wprm;
	struct play *play;
	int err;

	if (!player)
		return EINVAL;
	if (playp && *playp)
		return EALREADY;

	play = mem_zalloc(sizeof(*play), destructor);
	if (!play)
		return ENOMEM;

	tmr_init(&play->tmr);
	play->repeat = repeat ? repeat : 1;
	play->mb     = mem_ref(tone);

	err = mtx_init(&play->lock, mtx_plain) != thrd_success;
	if (err) {
		err = ENOMEM;
		goto out;
	}

	wprm.ch         = ch;
	wprm.srate      = srate;
	wprm.ptime      = PTIME;
	wprm.fmt        = AUFMT_S16LE;

	err = auplay_alloc(&play->auplay, baresip_auplayl(),
			   play_mod, &wprm,
			   play_dev, write_handler, play);
	if (err)
		goto out;

	list_append(&player->playl, &play->le, play);
	tmr_start(&play->tmr, PTIME,  tmr_polling, play);

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


static void ausrc_read_handler(struct auframe *af, void *arg)
{
	struct play *play = arg;
	int err = 0;

	if (play->eof)
		return;


	err = aubuf_write_auframe(play->aubuf, af);
	if (err)
		warning("play: aubuf_write: %m \n", err);
}


static void aubuf_write_handler(struct auframe *af, void *arg)
{
	struct play *play = arg;
	size_t sz = auframe_size(af);
	size_t left = aubuf_cur_size(play->aubuf);

	aubuf_read_auframe(play->aubuf, af);

	mtx_lock(&play->lock);
	if (!play->trep && !play->ausrc_st) {
		bool filling = left == aubuf_cur_size(play->aubuf);
		if (left < sz || filling)
			check_restart(play);
	}

	mtx_unlock(&play->lock);
}


static void ausrc_error_handler(int err, const char *str, void *arg)
{
	struct play *play = arg;
	(void)str;

	if (err == 0) {
		mtx_lock(&play->lock);
		play->ausrc_st = mem_deref(play->ausrc_st);
		mtx_unlock(&play->lock);
	}
}


static int start_ausrc(struct play *play)
{
	int err;
	const struct ausrc *ausrc = play->ausrc;

	if (!ausrc)
		return EINVAL;

	err = ausrc->alloch(&play->ausrc_st, ausrc, &play->sprm,
			play->filename,
			ausrc_read_handler, ausrc_error_handler, play);
	if (err)
		warning("play: could not start ausrc (%m)\n", err);

	return err;
}


static int start_auplay(struct play *play)
{
	struct auplay_prm wprm;
	int err;

	wprm.ch    = play->sprm.ch;
	wprm.srate = play->sprm.srate;
	wprm.ptime = play->sprm.ptime;
	wprm.fmt   = play->sprm.fmt;

	err = auplay_alloc(&play->auplay, baresip_auplayl(),
			   play->mod, &wprm,
			   play->dev, aubuf_write_handler, play);
	if (err)
		warning("play: could not start auplay (%m)\n", err);

	return err;
}


static int play_file_ausrc(struct play **playp,
		    const struct ausrc *ausrc,
		    const char *filename, int repeat,
		    const char *play_mod, const char *play_dev)
{
	int err = 0;
	size_t sampsz;
	struct ausrc_prm sprm;
	struct play *play;
	uint32_t srate = 0;
	uint32_t channels = 0;
	size_t minsz;
	size_t maxsz;

	conf_get_u32(conf_cur(), "file_srate", &srate);
	conf_get_u32(conf_cur(), "file_channels", &channels);

	play = mem_zalloc(sizeof(*play), destructor);
	if (!play)
		return ENOMEM;

	if (!srate)
		srate = 16000;

	if (!channels)
		channels = 1;

	err = mtx_init(&play->lock, mtx_plain) != thrd_success;
	if (err) {
		err = ENOMEM;
		goto out;
	}

	str_dup(&play->mod, play_mod);
	str_dup(&play->dev, play_dev);

	sprm.ch = channels;
	sprm.srate = srate;
	sprm.ptime = PTIME;
	sprm.fmt = AUFMT_S16LE;

	play->sprm = sprm;
	play->repeat = repeat ? repeat : 1;
	str_dup(&play->filename, filename);

	sampsz = aufmt_sample_size(sprm.fmt);
	minsz =  3 * sampsz * srate * channels * PTIME / 1000;
	maxsz = 24 * sampsz * srate * channels * PTIME / 1000;
	err = aubuf_alloc(&play->aubuf, minsz, maxsz);
	if (err)
		goto out;

	aubuf_set_live(play->aubuf, false);
	play->ausrc = ausrc;
	err = start_ausrc(play);
	if (err)
		goto out;

	tmr_start(&play->tmr, 4,  tmr_polling, play);

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


static void parse_play_settings(char *file, int *repeat, int *delay)
{
	struct pl f = PL_INIT;
	struct pl r = PL_INIT;
	struct pl d = PL_INIT;
	int err;

	if (!file || !repeat)
		return;

	err = re_regex(file, str_len(file), "[^,]+,[ ]*[^,]+,[ ]*[^,]+",
			&f, NULL, &r, NULL, &d);
	if (err)
		err = re_regex(file, str_len(file), "[^,]+,[ ]*[^,]+",
			       &f, NULL, &r);

	if (err || !pl_isset(&r))
		return;

	*repeat = (int) pl_u32(&r);
	if (*repeat == 0 && r.p[0] == '-')
		*repeat = -1;

	if (delay && pl_isset(&d))
		*delay = (int) pl_u32(&d);

	(void)pl_strcpy(&f, file, str_len(file));
}


/**
 * Play an audio file in WAV format
 *
 * @param playp    Pointer to allocated player object
 * @param player   Audio-file player
 * @param filename Name of WAV file to play
 * @param repeat   Number of times to repeat
 * @param play_mod Audio player module
 * @param play_dev Audio player device
 *
 * @return 0 if success, otherwise errorcode
 */
int play_file(struct play **playp, struct player *player,
	      const char *filename, int repeat,
	      const char *play_mod, const char *play_dev)
{
	const struct ausrc *ausrc;
	struct mbuf *mb = NULL;
	char *file = NULL;
	char *path = NULL;
	char *srcn = NULL;
	struct pl opt;
	int delay = 0;
	uint32_t srate = 0;
	uint8_t ch = 0;
	struct play *play = NULL;
	int err;

	if (!player)
		return EINVAL;
	if (playp && *playp)
		return EALREADY;

	err = re_sdprintf(&file, "%s", filename);
	if (err)
		goto out;

	parse_play_settings(file, &repeat, &delay);

	/* absolute path? */
	if (file[0] == '/' ||
	    !re_regex(file, strlen(file), "https://") ||
	    !re_regex(file, strlen(file), "http://") ||
	    !re_regex(file, strlen(file), "file://")) {

		err = re_sdprintf(&path, "%s", file);
	}
	else
		err = re_sdprintf(&path, "%s/%s", player->play_path, file);

	if (err)
		goto out;

	if (!conf_get(conf_cur(), "file_ausrc", &opt)) {

		err = pl_strdup(&srcn, &opt);
		if (err)
			goto out;

		ausrc = ausrc_find(baresip_ausrcl(), srcn);
		if (ausrc) {
			err = play_file_ausrc(&play, ausrc,
					      path, repeat,
					      play_mod, play_dev);

			goto out;
		}
	}

	mb = mbuf_alloc(1024);
	if (!mb) {
		err = ENOMEM;
		goto out;
	}

	err = aufile_load(mb, path, &srate, &ch);
	if (err)
		goto out;

	err = play_tone(&play, player, mb, srate,
	                ch, repeat, play_mod, play_dev);

 out:
	mem_deref(mb);

	mem_deref(file);
	mem_deref(path);
	mem_deref(srcn);

	if (play)
		play->delay = delay;

	if (err) {
		warning("play: %s (%m)\n", filename, err);
		mem_deref(play);
	}
	else if (play && playp) {
		play->playp = playp;
		*playp = play;
	}

	return err;
}


/**
 * Set the finish handler for given play state
 *
 * @param play The play state
 * @param fh   The finish handler
 * @param arg  Handler argument
 */
void play_set_finish_handler(struct play *play, play_finish_h *fh, void *arg)
{
	if (!play)
		return;

	play->fh  = fh;
	play->arg = arg;
}


static void player_destructor(void *data)
{
	struct player *player = data;

	list_flush(&player->playl);
}


/**
 * Initialize the audio player
 *
 * @param playerp Pointer to allocated player state
 *
 * @return 0 if success, otherwise errorcode
 */
int play_init(struct player **playerp)
{
	struct player *player;

	if (!playerp)
		return EINVAL;

	player = mem_zalloc(sizeof(*player), player_destructor);
	if (!player)
		return ENOMEM;

	list_init(&player->playl);

	str_ncpy(player->play_path, conf_config()->audio.audio_path,
			sizeof(player->play_path));

	*playerp = player;

	return 0;
}


/**
 * Set the path to the audio files
 *
 * @param player Player state
 * @param path   Path to audio files
 */
void play_set_path(struct player *player, const char *path)
{
	if (!player)
		return;

	str_ncpy(player->play_path, path, sizeof(player->play_path));
}
