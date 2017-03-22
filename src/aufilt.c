/**
 * @file aufilt.c Audio Filter
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <re.h>
#include <baresip.h>
#include "core.h"


void aufilt_register(struct list *aufiltl, struct aufilt *af)
{
	if (!aufiltl || !af)
		return;

	list_append(aufiltl, &af->le, af);

	info("aufilt: %s\n", af->name);
}


void aufilt_unregister(struct aufilt *af)
{
	if (!af)
		return;

	list_unlink(&af->le);
}
