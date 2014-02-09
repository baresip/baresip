/**
 * @file mda.h  Symbian MDA audio driver -- Internal interface
 *
 * Copyright (C) 2010 Creytiv.com
 */


int mda_player_alloc(struct auplay_st **stp, struct auplay *ap,
		     struct auplay_prm *prm, const char *device,
		     auplay_write_h *wh, void *arg);
int mda_recorder_alloc(struct ausrc_st **stp, struct ausrc *as,
		       struct media_ctx **ctx,
		       struct ausrc_prm *prm, const char *device,
		       ausrc_read_h *rh, ausrc_error_h *errh, void *arg);

int convert_srate(uint32_t srate);
int convert_channels(uint8_t ch);
