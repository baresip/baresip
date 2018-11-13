/**
 * @file winwave.h Windows sound driver -- internal api
 *
 * Copyright (C) 2010 Creytiv.com
 */


struct dspbuf {
	WAVEHDR      wh;
	struct mbuf *mb;
};


int winwave_src_alloc(struct ausrc_st **stp, const struct ausrc *as,
		      struct media_ctx **ctx,
		      struct ausrc_prm *prm, const char *device,
		      ausrc_read_h *rh, ausrc_error_h *errh, void *arg);
int winwave_play_alloc(struct auplay_st **stp, const struct auplay *ap,
		       struct auplay_prm *prm, const char *device,
		       auplay_write_h *wh, void *arg);
int winwave_enum_devices(const char *name, struct list *dev_list,
			 unsigned int *dev,
			 unsigned int (winwave_get_num_devs)(void),
			 int (winwave_get_dev_name)(unsigned int, char[32]));
int winwave_src_init(struct ausrc *as);
int winwave_player_init(struct auplay *ap);
unsigned winwave_get_format(enum aufmt fmt);
