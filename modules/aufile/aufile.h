/**
 * @file aufile.h WAV Audio Source/Player internal interface
 *
 * Copyright (C) 2020 commend.com - Christian Spielberger
 */

int aufile_play_alloc(struct auplay_st **stp, const struct auplay *ap,
		      struct auplay_prm *prm, const char *device,
		      auplay_write_h *wh, void *arg);
int aufile_src_alloc(struct ausrc_st **stp, const struct ausrc *as,
		     struct ausrc_prm *prm, const char *dev,
		     ausrc_read_h *rh, ausrc_error_h *errh, void *arg);
int aufile_info_handler(const struct ausrc *as,
			struct ausrc_prm *prm, const char *dev);
