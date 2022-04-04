/**
 * @file recorder.c  Pulseaudio sound driver - recorder (asynchronous API)
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

#define DEBUG_MODULE "pulse_async/recorder"
#define DEBUG_LEVEL 6
#include <re_dbg.h>


static void dev_list_cb(pa_context *context, const pa_source_info *l, int eol,
			void *arg)
{
	struct list *dev_list = arg;
	int err;
	(void)context;

	if (eol > 0)
		return;

	if (strstr(l->name, "output"))
		return;

	err = mediadev_add(dev_list, l->name);
	if (err)
		warning("pulse_async: record device %s could not be added\n",
			l->name);
}


static pa_operation *get_dev_info(pa_context *context, struct list *dev_list)
{
	return pa_context_get_source_info_list(context, dev_list_cb, dev_list);
}


int pulse_async_recorder_init(struct ausrc *as)
{
	if (!as)
		return EINVAL;

	list_init(&as->dev_list);
	return pulse_async_set_available_devices(&as->dev_list, get_dev_info);
}
