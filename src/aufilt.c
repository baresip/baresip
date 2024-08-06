/**
 * @file aufilt.c Audio Filter
 *
 * Copyright (C) 2010 Alfred E. Heggestad
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

	af->enabled = true;

	list_append(aufiltl, &af->le, af);

	info("aufilt: %s\n", af->name);
}


/**
 * Enable/Disable a Audio Filter
 *
 * @param aufiltl List of Audio Filters
 * @param name    Audio Filter name
 * @param enable  True for enable and False for disable
 */
void aufilt_enable(struct list *aufiltl, const char *name, bool enable)
{
	struct le *le;

	if (!aufiltl || !name)
		return;

	LIST_FOREACH(aufiltl, le) {
		struct aufilt *af = le->data;

		if (str_casecmp(af->name, name) != 0)
			continue;

		af->enabled = enable;
		break;
	}
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
