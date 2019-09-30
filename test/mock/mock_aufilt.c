/**
 * @file mock/mock_aufilt.c Mock audio filter
 *
 * Copyright (C) 2019 Creytiv.com
 */

#include <string.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "../test.h"


struct mock_enc {
	struct aufilt_enc_st af;  /* inheritance */
};


static void enc_destructor(void *arg)
{
	struct mock_enc *st = (struct mock_enc *)arg;

	list_unlink(&st->af.le);
}


static int mock_encode_update(struct aufilt_enc_st **stp, void **ctx,
			      const struct aufilt *af, struct aufilt_prm *prm,
			      const struct audio *au)
{
	struct mock_enc *st;
	(void)ctx;
	(void)au;

	if (!stp || !af || !prm)
		return EINVAL;

	if (*stp)
		return 0;

	st = (struct mock_enc *)mem_zalloc(sizeof(*st), enc_destructor);
	if (!st)
		return ENOMEM;

	*stp = (struct aufilt_enc_st *)st;

	return 0;
}


static int mock_encode(struct aufilt_enc_st *st, void *sampv, size_t *sampc)
{
	if (!st || !sampv || !sampc)
		return EINVAL;

	return 0;
}


static struct aufilt af_dummy = {
	.name    = "MOCK-AUFILT",
	.encupdh = mock_encode_update,
	.ench    = mock_encode,
};


void mock_aufilt_register(struct list *aufiltl)
{
	aufilt_register(aufiltl, &af_dummy);
}


void mock_aufilt_unregister(void)
{
	aufilt_unregister(&af_dummy);
}
