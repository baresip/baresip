/**
 * @file aufilt.c Audio Filter
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <re.h>
#include <baresip.h>
#include "core.h"


static struct list afl;


void aufilt_register(struct aufilt *af)
{
	if (!af)
		return;

	list_append(&afl, &af->le, af);

	info("aufilt: %s\n", af->name);
}


void aufilt_unregister(struct aufilt *af)
{
	if (!af)
		return;

	list_unlink(&af->le);
}


struct list *aufilt_list(void)
{
	return &afl;
}
