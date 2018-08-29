/**
 * @file pulse.h  Pulseaudio sound driver -- internal API
 *
 * Copyright (C) 2010 - 2016 Creytiv.com
 */


int pulse_player_alloc(struct auplay_st **stp, const struct auplay *ap,
		       struct auplay_prm *prm, const char *device,
		       auplay_write_h *wh, void *arg);
int pulse_recorder_alloc(struct ausrc_st **stp, const struct ausrc *as,
			 struct media_ctx **ctx,
			 struct ausrc_prm *prm, const char *device,
			 ausrc_read_h *rh, ausrc_error_h *errh, void *arg);
int set_available_devices(struct list *dev_list,
			  pa_operation *(get_dev_info_cb)(pa_context *,
							  struct list *));
int pulse_player_init(struct auplay *ap);
int pulse_recorder_init(struct ausrc *as);
