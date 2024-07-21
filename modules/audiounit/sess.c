/**
 * @file sess.c  AudioUnit sound driver - session
 *
 * Copyright (C) 2010 Alfred E. Heggestad
 */
#include <AudioUnit/AudioUnit.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "audiounit.h"


struct audiosess {
	struct list sessl;
};


struct audiosess_st {
	struct audiosess *as;
	struct le le;
	audiosess_int_h *inth;
	void *arg;
};


static struct audiosess *gas;


static void sess_destructor(void *arg)
{
	struct audiosess_st *st = arg;

	list_unlink(&st->le);
	mem_deref(st->as);
}


static void destructor(void *arg)
{
	struct audiosess *as = arg;

	list_flush(&as->sessl);

	gas = NULL;
}


int audiosess_alloc(struct audiosess_st **stp,
		    audiosess_int_h *inth, void *arg)
{
	struct audiosess_st *st = NULL;
	struct audiosess *as = NULL;
	int err = 0;
	bool created = false;

	if (!stp)
		return EINVAL;


	if (gas)
		goto makesess;

	as = mem_zalloc(sizeof(*as), destructor);
	if (!as)
		return ENOMEM;

	gas = as;
	created = true;

 makesess:
	st = mem_zalloc(sizeof(*st), sess_destructor);
	if (!st) {
		err = ENOMEM;
		goto out;
	}
	st->inth = inth;
	st->arg = arg;
	st->as = created ? gas : mem_ref(gas);

	list_append(&gas->sessl, &st->le, st);

 out:
	if (err) {
		mem_deref(as);
		mem_deref(st);
	}
	else {
		*stp = st;
	}

	return err;
}


void audiosess_interrupt(bool start)
{
	struct le *le;

	if (!gas)
		return;

	for (le = gas->sessl.head; le; le = le->next) {

		struct audiosess_st *st = le->data;

		if (st->inth)
			st->inth(start, st->arg);
	}
}
