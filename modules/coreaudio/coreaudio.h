/**
 * @file coreaudio.h  Apple Coreaudio sound driver -- internal API
 *
 * Copyright (C) 2010 Creytiv.com
 */


int  audio_session_enable(void);
void audio_session_disable(void);


int coreaudio_player_alloc(struct auplay_st **stp, const struct auplay *ap,
			   struct auplay_prm *prm, const char *device,
			   auplay_write_h *wh, void *arg);
int coreaudio_recorder_alloc(struct ausrc_st **stp, const struct ausrc *as,
			     struct media_ctx **ctx,
			     struct ausrc_prm *prm, const char *device,
			     ausrc_read_h *rh, ausrc_error_h *errh, void *arg);
int coreaudio_enum_devices(const char *name, struct list *dev_list,
			    CFStringRef *uid, Boolean is_input);
int coreaudio_player_init(struct auplay *ap);
int coreaudio_recorder_init(struct ausrc *as);
