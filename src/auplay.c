/**
 * @file auplay.c  Audio Player
 *
 * Copyright (C) 2010 Creytiv.com
 */

#include <re.h>
#include <baresip.h>
#include "core.h"


static void destructor(void *arg)
{
	struct auplay *ap = arg;

	list_flush(&ap->dev_list);
	list_unlink(&ap->le);
}


/**
 * Register an Audio Player
 *
 * @param app     Pointer to allocated Audio Player object
 * @param auplayl List of Audio Players
 * @param name    Audio Player name
 * @param alloch  Allocation handler
 *
 * @return 0 if success, otherwise errorcode
 */
int auplay_register(struct auplay **app, struct list *auplayl,
		    const char *name, auplay_alloc_h *alloch)
{
	struct auplay *ap;

	if (!app)
		return EINVAL;

	ap = mem_zalloc(sizeof(*ap), destructor);
	if (!ap)
		return ENOMEM;

	list_append(auplayl, &ap->le, ap);

	ap->name   = name;
	ap->alloch = alloch;

	info("auplay: %s\n", name);

	*app = ap;

	return 0;
}


/**
 * Find an Audio Player by name
 *
 * @param auplayl List of Audio Players
 * @param name    Name of the Audio Player to find
 *
 * @return Matching Audio Player if found, otherwise NULL
 */
const struct auplay *auplay_find(const struct list *auplayl, const char *name)
{
	struct le *le;

	for (le=list_head(auplayl); le; le=le->next) {

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
 * @param stp     Pointer to allocated Audio Player state
 * @param auplayl List of Audio Players
 * @param name    Name of Audio Player
 * @param prm     Audio Player parameters
 * @param device  Name of Audio Player device (driver specific)
 * @param wh      Write handler
 * @param arg     Handler argument
 *
 * @return 0 if success, otherwise errorcode
 */
int auplay_alloc(struct auplay_st **stp, struct list *auplayl,
		 const char *name,
		 struct auplay_prm *prm, const char *device,
		 auplay_write_h *wh, void *arg)
{
	struct auplay *ap;

	ap = (struct auplay *)auplay_find(auplayl, name);
	if (!ap)
		return ENOENT;

	if (!prm->srate || !prm->ch)
		return EINVAL;

	return ap->alloch(stp, ap, prm, device, wh, arg);
}
