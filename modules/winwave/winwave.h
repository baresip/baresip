/**
 * @file winwave.h Windows sound driver -- internal api
 *
 * Copyright (C) 2010 Creytiv.com
 */


struct dspbuf {
	WAVEHDR      wh;
	struct mbuf *mb;
};


int winwave_src_alloc(struct ausrc_st **stp, struct ausrc *as,
		      struct media_ctx **ctx,
		      struct ausrc_prm *prm, const char *device,
		      ausrc_read_h *rh, ausrc_error_h *errh, void *arg);
int winwave_play_alloc(struct auplay_st **stp, struct auplay *ap,
		       struct auplay_prm *prm, const char *device,
		       auplay_write_h *wh, void *arg);
