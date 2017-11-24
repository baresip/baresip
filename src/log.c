/**
 * @file log.c Logging
 *
 * Copyright (C) 2010 Creytiv.com
 */

#include <re.h>
#include <baresip.h>


static struct {
	struct list logl;
	bool debug;
	bool info;
	bool stder;
} lg = {
	LIST_INIT,
	false,
	true,
	true
};


void log_register_handler(struct log *log)
{
	if (!log)
		return;

	list_append(&lg.logl, &log->le, log);
}


void log_unregister_handler(struct log *log)
{
	if (!log)
		return;

	list_unlink(&log->le);
}


void log_enable_debug(bool enable)
{
	lg.debug = enable;
}


void log_enable_info(bool enable)
{
	lg.info = enable;
}


void log_enable_stderr(bool enable)
{
	lg.stder = enable;
}


void vlog(enum log_level level, const char *fmt, va_list ap)
{
	char buf[4096];
	struct le *le;

	if (re_vsnprintf(buf, sizeof(buf), fmt, ap) < 0)
		return;

	if (lg.stder) {

		bool color = level == LEVEL_WARN || level == LEVEL_ERROR;

		if (color)
			(void)re_fprintf(stdout, "\x1b[31m"); /* Red */

		(void)re_fprintf(stdout, "%s", buf);

		if (color)
			(void)re_fprintf(stdout, "\x1b[;m");
	}

	le = lg.logl.head;

	while (le) {

		struct log *log = le->data;
		le = le->next;

		if (log->h)
			log->h(level, buf);
	}
}


void loglv(enum log_level level, const char *fmt, ...)
{
	va_list ap;

	if ((LEVEL_DEBUG == level) && !lg.debug)
		return;

	if ((LEVEL_INFO == level) && !lg.info)
		return;

	va_start(ap, fmt);
	vlog(level, fmt, ap);
	va_end(ap);
}


void debug(const char *fmt, ...)
{
	va_list ap;

	if (!lg.debug)
		return;

	va_start(ap, fmt);
	vlog(LEVEL_DEBUG, fmt, ap);
	va_end(ap);
}


void info(const char *fmt, ...)
{
	va_list ap;

	if (!lg.info)
		return;

	va_start(ap, fmt);
	vlog(LEVEL_INFO, fmt, ap);
	va_end(ap);
}


void warning(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vlog(LEVEL_WARN, fmt, ap);
	va_end(ap);
}


void error_msg(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vlog(LEVEL_ERROR, fmt, ap);
	va_end(ap);
}
