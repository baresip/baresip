/**
 * @file syslog.c Syslog module
 *
 * Copyright (C) 2010 Creytiv.com
 */
#define _BSD_SOURCE 1
#define _GNU_SOURCE 1
#include <stdio.h>
#include <syslog.h>
#include <re.h>
#include <baresip.h>


#define DEBUG_MODULE "syslog"
#define DEBUG_LEVEL 6
#include <re_dbg.h>


#if defined (DARWIN) || defined (__GLIBC__)

static FILE *fv[2];


static int writer(void *cookie, const char *p, int len)
{
	(void)cookie;

	syslog(LOG_NOTICE, "%.*s", (int)len, p);

	return len;
}


static void tolog(int ix, FILE **pfp)
{
#if defined (__GLIBC__)
	static cookie_io_functions_t memfile_func = {
		.write = (cookie_write_function_t *)writer
	};
#endif
	FILE *f;

#if defined (__GLIBC__)
	f = fopencookie(NULL, "w+", memfile_func);
#else
	f = fwopen(NULL, writer);
#endif

	if (!f)
		return;

	setvbuf(f, NULL, _IOLBF, 0);
	fv[ix] = *pfp = f;
}


static void restore(int ix, FILE **fp)
{
	if (fv[ix]) {
		*fp = fv[ix];
		fv[ix] = NULL;
	}
}
#endif


static void syslog_handler(int level, const char *p, size_t len, void *arg)
{
	(void)arg;

	syslog(level, "%.*s", (int)len, p);
}


static int module_init(void)
{
	openlog("baresip", LOG_NDELAY | LOG_PID, LOG_LOCAL0);

#if defined (DARWIN) || defined (__GLIBC__)
	/* Redirect stdout/stderr to syslog */
	tolog(0, &stdout);
	tolog(1, &stderr);
#endif

	dbg_init(DBG_INFO, DBG_NONE);
	dbg_handler_set(syslog_handler, NULL);

	return 0;
}


static int module_close(void)
{
	dbg_handler_set(NULL, NULL);

#if defined (DARWIN) || defined (__GLIBC__)
	restore(0, &stdout);
	restore(1, &stderr);
#endif

	closelog();

	return 0;
}


const struct mod_export DECL_EXPORTS(syslog) = {
	"syslog",
	"syslog",
	module_init,
	module_close
};
