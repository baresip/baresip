/**
 * @file mod_jack.h  JACK audio driver -- internal api
 *
 * Copyright (C) 2010 Creytiv.com
 */


int jack_play_alloc(struct auplay_st **stp, const struct auplay *ap,
		    struct auplay_prm *prm, const char *device,
		    auplay_write_h *wh, void *arg);
int jack_src_alloc(struct ausrc_st **stp, const struct ausrc *as,
		   struct media_ctx **ctx,
		   struct ausrc_prm *prm, const char *device,
		   ausrc_read_h *rh, ausrc_error_h *errh, void *arg);
