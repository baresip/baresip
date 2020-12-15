/**
 * @file commod.c Commend application module
 *
 * Copyright (C) 2020 Commend.com - c.spielberger@commend.com
 */

#include <re.h>
#include <baresip.h>


/**
 * @defgroup commod commod
 *
 * This module implements Commend specific commands
 */


#define DEBUG_MODULE "commod"
#define DEBUG_LEVEL 5
#include <re_dbg.h>



static int module_init(void)
{
	int err;

	err  = cmd_register(baresip_commands(), cmdv, ARRAY_SIZE(cmdv));

	return err;
}


static int module_close(void)
{

	cmd_unregister(baresip_commands(), cmdv);

	return 0;
}


const struct mod_export DECL_EXPORTS(commod) = {
	"commod",
	"application",
	module_init,
	module_close
};
