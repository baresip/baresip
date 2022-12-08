/**
 * @file tone.h module menu internal API for playing tones
 *
 * Copyright (c) 2022 Commend.com - c.spielberger@commend.com
 */

enum Device {
	DEVICE_ALERT,
	DEVICE_PLAYER
};

void tone_init(void);
void tone_stop(void);
void tone_play(const struct pl *pl, int repeat, enum Device device);
void tone_set_finish_handler(play_finish_h *fh, void *arg);
