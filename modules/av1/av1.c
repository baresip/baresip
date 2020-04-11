/**
 * @file av1.c AV1 Video Codec
 *
 * Copyright (C) 2010 - 2016 Creytiv.com
 */
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "av1.h"


/**
 * @defgroup av1 av1
 *
 * The AV1 video codec (Experimental)
 *
 * Reference: http://aomedia.org/
 */


static struct vidcodec av1 = {
	.name      = "AV1",
	.encupdh   = av1_encode_update,
	.ench      = av1_encode_packet,
	.decupdh   = av1_decode_update,
	.dech      = av1_decode,
};


static int module_init(void)
{
	vidcodec_register(baresip_vidcodecl(), &av1);

	return 0;
}


static int module_close(void)
{
	vidcodec_unregister(&av1);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(av1) = {
	"av1",
	"codec",
	module_init,
	module_close
};
