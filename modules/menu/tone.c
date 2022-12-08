/**
 * @file tone.c  Signal tones
 *
 * Copyright (C) 2022 Commend.com - c.spielberger@commend.com
 */
#include <string.h>
#include <re.h>
#include <baresip.h>
#include "tone.h"

enum {
	DEBOUNCE_DELAY =   20,
};

struct {
	struct play *play;            /**< Current audio player state     */
	int repeat;
	char *filename;
	enum Device device;
	struct tmr tmr_play;
} tone;


void tone_init(void)
{
	memset(&tone, 0, sizeof(tone));
}


static void do_play(void *arg)
{
	enum Device device = tone.device;
	struct config *cfg = conf_config();
	struct player *player = baresip_player();
	const char *play_mod = cfg->audio.alert_mod;
	const char *play_dev = cfg->audio.alert_dev;
	(void) arg;

	if (device == DEVICE_PLAYER) {
		play_mod = cfg->audio.play_mod;
		play_dev = cfg->audio.play_dev;
	}

	play_file(&tone.play, player, tone.filename, tone.repeat,
		  play_mod, play_dev);

	tone.filename = mem_deref(tone.filename);
}


void tone_stop(void)
{
	tmr_cancel(&tone.tmr_play);

	tone.filename = mem_deref(tone.filename);
	tone.play = mem_deref(tone.play);
}


void tone_play(const struct pl *pl, int repeat, enum Device device)
{
	if (!pl_isset(pl))
		return;

	pl_strdup(&tone.filename, pl);
	tone.repeat = repeat;
	tone.device = device;

	tmr_start(&tone.tmr_play, DEBOUNCE_DELAY, do_play, NULL);
}


void tone_set_finish_handler(play_finish_h *fh, void *arg)
{
	play_set_finish_handler(tone.play, fh, arg);
}
