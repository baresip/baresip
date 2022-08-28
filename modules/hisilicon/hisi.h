/**
 * @file hisi.c  HiSilicon sound driver
 *
 * Copyright (C) 2022 Dmitry Ilyin
 */


extern char alsa_dev[64];


unsigned audio_frame_size(unsigned srate);
int hisi_src_alloc(struct ausrc_st **stp, const struct ausrc *as,
		   struct ausrc_prm *prm, const char *device,
		   ausrc_read_h *rh, ausrc_error_h *errh, void *arg);
int hisi_play_alloc(struct auplay_st **stp, const struct auplay *ap,
		    struct auplay_prm *prm, const char *device,
		    auplay_write_h *wh, void *arg);
