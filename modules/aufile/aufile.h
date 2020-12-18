/**
 * @file aufile.h WAV Audio Source/Player internal interface
 *
 * Copyright (C) 2020 commend.com - Christian Spielberger
 */

int play_alloc(struct auplay_st **stp, const struct auplay *ap,
		    struct auplay_prm *prm, const char *device,
		    auplay_write_h *wh, void *arg);
