/**
 * @file player.c  Pulseaudio sound driver - player (asynchronous API)
 *
 * Copyright (C) 2021 Commend.com - h.ramoser@commend.com,
 *                                  c.spielberger@commend.com
 */
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <string.h>
#include <pulse/pulseaudio.h>
#include "pulse.h"

#define DEBUG_MODULE "pulse_async/player"
#define DEBUG_LEVEL 6
#include <re_dbg.h>


static void dev_list_cb(pa_context *context, const pa_sink_info *l, int eol,
			void *arg)
{
	struct list *dev_list = arg;
	int err;
	(void)context;

	if (eol > 0)
		return;

	err = mediadev_add(dev_list, l->name);
	if (err)
		warning("pulse_async: playback device %s could not be added\n",
			l->name);
}


static pa_operation *get_dev_info(pa_context *context, struct list *dev_list)
{
	return pa_context_get_sink_info_list(context, dev_list_cb, dev_list);
}


int pulse_async_player_init(struct auplay *ap)
{
	if (!ap)
		return EINVAL;

	list_init(&ap->dev_list);
	return pulse_async_set_available_devices(&ap->dev_list, get_dev_info);
}
