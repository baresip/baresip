/**
 * @file coreaudio.h  Apple Coreaudio sound driver -- internal API
 *
 * Copyright (C) 2010 Creytiv.com
 */


int  audio_session_enable(void);
void audio_session_disable(void);

int audio_fmt(enum aufmt fmt);
int bytesps(enum aufmt fmt);

int coreaudio_player_alloc(struct auplay_st **stp, struct auplay *ap,
			   struct auplay_prm *prm, const char *device,
			   auplay_write_h *wh, void *arg);
int coreaudio_recorder_alloc(struct ausrc_st **stp, struct ausrc *as,
			     struct media_ctx **ctx,
			     struct ausrc_prm *prm, const char *device,
			     ausrc_read_h *rh, ausrc_error_h *errh, void *arg);
