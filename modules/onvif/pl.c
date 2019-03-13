/**
 * @file pl.c
 *
 * Copyright (C) 2020 commend.com - Christian Spielberger
 */

#include <string.h>
#include <re.h>
#include "pl.h"


/**
 * Initialise a pointer-length object from a string with defined length
 *
 * @param pl	Pointer-length object to be initialised
 * @param str string
 * @param n		length
 */
void pl_set_n_str(struct pl *pl, const char *str, int n)
{
	if (!pl || !str)
		return;

	pl->p = str;
	pl->l = n;
}



/**
 * Locate string in pointer-length string
 *
 * @param pl		Pointer-length string
 * @param str		String to locate
 *
 * @return			Pointer to the fist char of @str in @pl
 *							if found, otherwise NULL
 */

const char *pl_strstr(const struct pl *pl, const char *str)
{
	size_t lencount = 0;

	if (!pl || !str)
		return NULL;

	for (lencount = 0; lencount < pl->l; lencount++) {
		if (strlen(str) > (pl->l - lencount))
			return NULL;

		if (!strncmp((pl->p + lencount), str, strlen(str)))
			return pl->p + lencount;
	}

	return NULL;
}
