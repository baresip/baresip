/**
 * @file syslog.c Syslog module
 *
 * Copyright (C) 2010 Creytiv.com
 */

#include <syslog.h>
#include <re.h>
#include <baresip.h>


/**
 * @defgroup syslog syslog
 *
 * This module implements a logging handler for output to syslog
 */


#define DEBUG_MODULE ""
#define DEBUG_LEVEL 0
#include <re_dbg.h>


static const int lmap[] = { LOG_DEBUG, LOG_INFO, LOG_WARNING, LOG_ERR };


static void log_handler(uint32_t level, const char *msg)
{
	syslog(lmap[MIN(level, ARRAY_SIZE(lmap)-1)], "%s", msg);
}


static struct log lg = {
	.h = log_handler,
};


static void syslog_handler(int level, const char *p, size_t len, void *arg)
{
	(void)arg;

	syslog(level, "%.*s", (int)len, p);
}


static int module_init(void)
{
	openlog("baresip", LOG_NDELAY | LOG_PID, LOG_LOCAL0);

	dbg_init(DBG_INFO, DBG_NONE);
	dbg_handler_set(syslog_handler, NULL);

	log_register_handler(&lg);

	return 0;
}


static int module_close(void)
{
	log_unregister_handler(&lg);

	dbg_handler_set(NULL, NULL);

	closelog();

	return 0;
}


const struct mod_export DECL_EXPORTS(syslog) = {
	"syslog",
	"application",
	module_init,
	module_close
};
