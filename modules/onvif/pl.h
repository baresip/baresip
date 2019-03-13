/* @file pl.h
 *
 * Copyright (C) 2020 commend.com - Christian Spielberger
 */

#ifndef _ONVIF_PL_H_
#define _ONVIF_PL_H_


struct pl;
const char *pl_strstr(const struct pl *pl, const char *str);
void pl_set_n_str(struct pl *pl, const char *str, int n);

#endif

