/**
 * @file pipewire.h  Pipewire sound driver - internal API
 *
 * Copyright (C) 2023 Commend.com - c.spielberger@commend.com
 */

int aufmt_to_pw_format(enum aufmt fmt);
struct pw_core *pw_core_instance(void);
struct pw_thread_loop *pw_loop_instance(void);
int pw_device_id(const char *node_name);

int pw_playback_alloc(struct auplay_st **stp,
		      const struct auplay *ap,
		      struct auplay_prm *prm, const char *dev,
		      auplay_write_h *wh, void *arg);
int pw_capture_alloc(struct ausrc_st **stp, const struct ausrc *as,
		     struct ausrc_prm *prm, const char *dev, ausrc_read_h *rh,
		     ausrc_error_h *errh, void *arg);
