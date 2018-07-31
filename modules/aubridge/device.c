/**
 * @file device.c Audio bridge -- virtual device table
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <pthread.h>
#include "aubridge.h"


/* The packet-time is fixed to 20 milliseconds */
enum {PTIME = 20};


struct device {
	struct le le;
	const struct ausrc_st *ausrc;
	const struct auplay_st *auplay;
	char name[64];
	pthread_t thread;
	volatile bool run;
};


static void destructor(void *arg)
{
	struct device *dev = arg;

	device_stop(dev);

	list_unlink(&dev->le);
}


static bool list_apply_handler(struct le *le, void *arg)
{
	struct device *st = le->data;

	return 0 == str_cmp(st->name, arg);
}


static struct device *find_device(const char *device)
{
	return list_ledata(hash_lookup(ht_device, hash_joaat_str(device),
				       list_apply_handler, (void *)device));
}


static void *device_thread(void *arg)
{
	uint64_t now, ts = tmr_jiffies();
	struct device *dev = arg;
	struct auresamp rs;
	int16_t *sampv_in, *sampv_out;
	size_t sampc_in;
	size_t sampc_out;
	int err;

	if (!dev->run) {
		return NULL;
	}

	sampc_in = dev->auplay->prm.srate * dev->auplay->prm.ch * PTIME/1000;
	sampc_out = dev->ausrc->prm.srate * dev->ausrc->prm.ch * PTIME/1000;

	auresamp_init(&rs);

	sampv_in  = mem_alloc(2 * sampc_in, NULL);
	sampv_out = mem_alloc(2 * sampc_out, NULL);
	if (!sampv_in || !sampv_out)
		goto out;

	err = auresamp_setup(&rs,
			     dev->auplay->prm.srate, dev->auplay->prm.ch,
			     dev->ausrc->prm.srate, dev->ausrc->prm.ch);
	if (err)
		goto out;

	while (dev->run) {

		(void)sys_msleep(4);

		if (!dev->run)
			break;

		now = tmr_jiffies();

		if (ts > now)
			continue;

		if (dev->auplay && dev->auplay->wh) {
			dev->auplay->wh(sampv_in, sampc_in, dev->auplay->arg);
		}

		if (rs.resample) {
			err = auresamp(&rs,
				       sampv_out, &sampc_out,
				       sampv_in, sampc_in);
			if (err) {
				warning("aubridge: auresamp error"
					" sampc_out=%zu, sampc_in=%zu (%m)\n",
					sampc_out, sampc_in, err);
			}

			if (dev->ausrc && dev->ausrc->rh) {
				dev->ausrc->rh(sampv_out, sampc_out,
					       dev->ausrc->arg);
			}
		}
		else {
			if (dev->ausrc && dev->ausrc->rh) {
				dev->ausrc->rh(sampv_in, sampc_in,
					       dev->ausrc->arg);
			}
		}

		ts += PTIME;
	}

 out:
	mem_deref(sampv_in);
	mem_deref(sampv_out);

	return NULL;
}


int device_connect(struct device **devp, const char *device,
		   struct auplay_st *auplay, struct ausrc_st *ausrc)
{
	struct device *dev;
	int err = 0;

	if (!devp)
		return EINVAL;
	if (!str_isset(device))
		return ENODEV;

	dev = find_device(device);
	if (dev) {
		*devp = mem_ref(dev);
	}
	else {
		dev = mem_zalloc(sizeof(*dev), destructor);
		if (!dev)
			return ENOMEM;

		str_ncpy(dev->name, device, sizeof(dev->name));

		hash_append(ht_device, hash_joaat_str(device), &dev->le, dev);

		*devp = dev;

		info("aubridge: created device '%s'\n", device);
	}

	if (auplay)
		dev->auplay = auplay;
	if (ausrc)
		dev->ausrc = ausrc;

	/* wait until we have both SRC+PLAY */
	if (dev->ausrc && dev->auplay && !dev->run) {

		dev->run = true;
		err = pthread_create(&dev->thread, NULL, device_thread, dev);
		if (err) {
			dev->run = false;
		}
	}

	return err;
}


void device_stop(struct device *dev)
{
	if (!dev)
		return;

	dev->auplay = NULL;
	dev->ausrc = NULL;

	if (dev->run) {
		dev->run = false;
		pthread_join(dev->thread, NULL);
	}
}
