/**
 * @file snapshot.c  Snapshot Video-Filter
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <string.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>

static char *make_filename(const char *name,
			  char *buf, unsigned int length, const char *ext);


static int make_preview(const struct vidframe **preview,const struct vidframe *frame);
