/**
 * @file wasapi.c Windows Audio Session API (WASAPI)
 *
 * Copyright (C) 2024 Sebastian Reimers
 * Copyright (C) 2024 AGFEO GmbH & Co. KG
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audiosessiontypes.h>
#include <audiopolicy.h>


int wasapi_play_alloc(struct auplay_st **stp, const struct auplay *ap,
		      struct auplay_prm *prm, const char *device,
		      auplay_write_h *wh, void *arg);
int wasapi_src_alloc(struct ausrc_st **stp, const struct ausrc *as,
		      struct ausrc_prm *prm, const char *device,
		      ausrc_read_h *rh, ausrc_error_h *errh, void *arg);
