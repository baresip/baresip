/**
 * @file aufilt.c Audio Filter
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <re.h>
#include <baresip.h>
#include "core.h"


/**
 * Register an Audio Filter
 *
 * @param aufiltl List of Audio Filters
 * @param af      Audio Filter to register
 */
void aufilt_register(struct list *aufiltl, struct aufilt *af)
{
	if (!aufiltl || !af)
		return;

	list_append(aufiltl, &af->le, af);

	info("aufilt: %s\n", af->name);
}


/**
 * Unregister an Audio Filter
 *
 * @param af Audio Filter to unregister
 */
void aufilt_unregister(struct aufilt *af)
{
	if (!af)
		return;

	list_unlink(&af->le);
}
