/**
 * @file aaudio/aaudio.h AAudio audio driver for Android
 *
 * Copyright (C) 2024 Juha Heinanen
 */

#include <aaudio/AAudio.h>

void aaudio_close_stream(AAudioStream *stream);

int aaudio_player_alloc(struct auplay_st **stp, const struct auplay *ap,
			struct auplay_prm *prm, const char *device,
			auplay_write_h *wh, void *arg);

int aaudio_recorder_alloc(struct ausrc_st **stp, const struct ausrc *as,
			  struct ausrc_prm *prm, const char *device,
			  ausrc_read_h *rh, ausrc_error_h *errh, void *arg);
