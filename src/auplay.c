/**
 * @file auplay.c  Audio Player
 *
 * Copyright (C) 2010 Creytiv.com
 */

#include <re.h>
#include <baresip.h>
#include "core.h"


static struct list auplayl = LIST_INIT;


static void destructor(void *arg)
{
	struct auplay *ap = arg;

	list_unlink(&ap->le);
}


/**
 * Register an Audio Player
 *
 * @param app     Pointer to allocated Audio Player object
 * @param name    Audio Player name
 * @param alloch  Allocation handler
 *
 * @return 0 if success, otherwise errorcode
 */
int auplay_register(struct auplay **app, const char *name,
		    auplay_alloc_h *alloch)
{
	struct auplay *ap;

	if (!app)
		return EINVAL;

	ap = mem_zalloc(sizeof(*ap), destructor);
	if (!ap)
		return ENOMEM;

	list_append(&auplayl, &ap->le, ap);

	ap->name   = name;
	ap->alloch = alloch;

	info("auplay: %s\n", name);

	*app = ap;

	return 0;
}


/**
 * Find an Audio Player by name
 *
 * @param name Name of the Audio Player to find
 *
 * @return Matching Audio Player if found, otherwise NULL
 */
const struct auplay *auplay_find(const char *name)
{
	struct le *le;

	for (le=auplayl.head; le; le=le->next) {

		struct auplay *ap = le->data;

		if (str_isset(name) && 0 != str_casecmp(name, ap->name))
			continue;

		return ap;
	}

	return NULL;
}


/**
 * Allocate an Audio Player state
 *
 * @param stp    Pointer to allocated Audio Player state
 * @param name   Name of Audio Player
 * @param prm    Audio Player parameters
 * @param device Name of Audio Player device (driver specific)
 * @param wh     Write handler
 * @param arg    Handler argument
 *
 * @return 0 if success, otherwise errorcode
 */
int auplay_alloc(struct auplay_st **stp, const char *name,
		 struct auplay_prm *prm, const char *device,
		 auplay_write_h *wh, void *arg)
{
	struct auplay *ap;

	ap = (struct auplay *)auplay_find(name);
	if (!ap)
		return ENOENT;

	if (!prm->srate || !prm->ch)
		return EINVAL;

	return ap->alloch(stp, ap, prm, device, wh, arg);
}
