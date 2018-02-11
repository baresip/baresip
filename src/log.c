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
	bool enable_stdout;
} lg = {
	LIST_INIT,
	false,
	true,
	true
};


/**
 * Register a log handler
 *
 * @param log Log handler
 */
void log_register_handler(struct log *log)
{
	if (!log)
		return;

	list_append(&lg.logl, &log->le, log);
}


/**
 * Unregister a log handler
 *
 * @param log Log handler
 */
void log_unregister_handler(struct log *log)
{
	if (!log)
		return;

	list_unlink(&log->le);
}


/**
 * Enable debug-level logging
 *
 * @param enable True to enable, false to disable
 */
void log_enable_debug(bool enable)
{
	lg.debug = enable;
}


/**
 * Enable info-level logging
 *
 * @param enable True to enable, false to disable
 */
void log_enable_info(bool enable)
{
	lg.info = enable;
}


/**
 * Enable logging to standard-out
 *
 * @param enable True to enable, false to disable
 */
void log_enable_stdout(bool enable)
{
	lg.enable_stdout = enable;
}


/**
 * Print a message to the logging system
 *
 * @param level Log level
 * @param fmt   Formatted message
 * @param ap    Variable argument list
 */
void vlog(enum log_level level, const char *fmt, va_list ap)
{
	char buf[4096];
	struct le *le;

	if (re_vsnprintf(buf, sizeof(buf), fmt, ap) < 0)
		return;

	if (lg.enable_stdout) {

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


/**
 * Print a message to the logging system
 *
 * @param level Log level
 * @param fmt   Formatted message
 * @param ...   Variable arguments
 */
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


/**
 * Print a DEBUG message to the logging system
 *
 * @param fmt   Formatted message
 * @param ...   Variable arguments
 */
void debug(const char *fmt, ...)
{
	va_list ap;

	if (!lg.debug)
		return;

	va_start(ap, fmt);
	vlog(LEVEL_DEBUG, fmt, ap);
	va_end(ap);
}


/**
 * Print an INFO message to the logging system
 *
 * @param fmt   Formatted message
 * @param ...   Variable arguments
 */
void info(const char *fmt, ...)
{
	va_list ap;

	if (!lg.info)
		return;

	va_start(ap, fmt);
	vlog(LEVEL_INFO, fmt, ap);
	va_end(ap);
}


/**
 * Print a WARNING message to the logging system
 *
 * @param fmt   Formatted message
 * @param ...   Variable arguments
 */
void warning(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vlog(LEVEL_WARN, fmt, ap);
	va_end(ap);
}


/**
 * Print an ERROR message to the logging system
 *
 * @param fmt   Formatted message
 * @param ...   Variable arguments
 */
void error_msg(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vlog(LEVEL_ERROR, fmt, ap);
	va_end(ap);
}
