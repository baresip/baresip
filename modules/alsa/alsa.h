/**
 * @file alsa.h  ALSA sound driver -- internal interface
 *
 * Copyright (C) 2010 Creytiv.com
 */


extern char alsa_dev[64];


int alsa_reset(snd_pcm_t *pcm, uint32_t srate, uint32_t ch,
	       uint32_t num_frames, snd_pcm_format_t pcmfmt);
snd_pcm_format_t aufmt_to_alsaformat(enum aufmt fmt);
int alsa_src_alloc(struct ausrc_st **stp, const struct ausrc *as,
		   struct media_ctx **ctx,
		   struct ausrc_prm *prm, const char *device,
		   ausrc_read_h *rh, ausrc_error_h *errh, void *arg);
int alsa_play_alloc(struct auplay_st **stp, const struct auplay *ap,
		    struct auplay_prm *prm, const char *device,
		    auplay_write_h *wh, void *arg);
