/**
 * @file mediadev.c  Media device
 *
 * Copyright (C) 2010 - 2018 Creytiv.com
 */
#include <re.h>
#include <baresip.h>


static void destructor(void *data)
{
	struct mediadev *dev = data;

	mem_deref(dev->name);
}


int mediadev_add(struct list *dev_list, const char *name)
{
	struct mediadev *dev;
	int err;

	if (!dev_list || !str_isset(name))
		return EINVAL;

	if (mediadev_find(dev_list, name)) {
		return 0;
	}

	dev = mem_zalloc(sizeof(*dev), destructor);
	if (!dev)
		return ENOMEM;

	err = str_dup(&dev->name, name);
	if (err)
		goto out;

	list_append(dev_list, &dev->le, dev);

 out:
	if (err)
		mem_deref(dev);

	return err;
}


struct mediadev *mediadev_find(const struct list *dev_list, const char *name)
{
	struct le *le;

	for (le = list_head(dev_list); le; le = le->next) {

		struct mediadev *dev = le->data;

		if (!str_casecmp(dev->name, name))
			return dev;
	}

	return NULL;
}


struct mediadev *mediadev_get_default(const struct list *dev_list)
{
	struct le *le;

	if (!dev_list)
		return NULL;

	le = list_head(dev_list);
	if (le) {
		return le->data;
	}

	return NULL;
}


int mediadev_print(struct re_printf *pf, const struct list *dev_list)
{
	struct le *le;
	int err;

	if (!dev_list)
		return 0;

	err = re_hprintf(pf, "Devices: (%u)\n", list_count(dev_list));

	for (le = list_head(dev_list); le; le = le->next) {

		struct mediadev *dev = le->data;

		err |= re_hprintf(pf, "%s\n", dev->name);
	}

	return err;
}
