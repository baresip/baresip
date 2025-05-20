/**
 * @file log.c Logging
 *
 * Copyright (C) 2010 Alfred E. Heggestad
 */

#include <re.h>
#include <baresip.h>


static struct {
	struct list logl;
	enum log_level level;
	bool enable_stdout;
	bool timestamps;
	bool color;
} lg = {
	LIST_INIT,
	LEVEL_INFO,
	true,
	false,
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
 * Set the current log level
 *
 * @param level Log level
 */
void log_level_set(enum log_level level)
{
	lg.level = level;
}


/**
 * Get the current log level
 *
 * @return Log level
 */
enum log_level log_level_get(void)
{
	return lg.level;
}


/**
 * Get the log level as a string
 *
 * @param level Log level
 *
 * @return String with log level name
 */
const char *log_level_name(enum log_level level)
{
	switch (level) {

	case LEVEL_DEBUG: return "DEBUG";
	case LEVEL_INFO:  return "INFO";
	case LEVEL_WARN:  return "WARNING";
	case LEVEL_ERROR: return "ERROR";
	default: return "???";
	}
}

/**
 * Enable debug-level logging
 *
 * @param enable True to enable, false to disable
 */
void log_enable_debug(bool enable)
{
	lg.level = enable ? LEVEL_DEBUG : LEVEL_INFO;
}


/**
 * Enable info-level logging
 *
 * @param enable True to enable, false to disable
 */
void log_enable_info(bool enable)
{
	lg.level = enable ? LEVEL_INFO : LEVEL_WARN;
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
 * Enable timestamps for logging
 *
 * @param enable True to enable, false to disable
 */
void log_enable_timestamps(bool enable)
{
	lg.timestamps = enable;
}


/**
 * Enable/disable colored warnings and errors
 *
 * @param enable True to enable, false to disable
 */
void log_enable_color(bool enable)
{
	lg.color = enable;
}


/**
 * Print a message to the logging system
 *
 * @param level Log level
 * @param fmt   Formatted message
 * @param ap    Variable argument list
 */
static void vlog(bool safe, enum log_level level, const char *fmt,
		 va_list ap)
{
	char buf[8192];
	char *p = buf;
	size_t s = sizeof(buf);
	int n;
	struct le *le;

	if (level < lg.level)
		return;

	if (lg.timestamps) {
		n = re_snprintf(p, s, "%H|", fmt_timestamp, NULL);
		if (n < 0)
			return;

		p += n;
		s -= n;
	}

	if (safe) {
		if (re_vsnprintf_s(p, s, fmt, ap) < 0)
			return;
	}
	else {
		if (re_vsnprintf(p, s, fmt, ap) < 0)
			return;
	}

	if (lg.enable_stdout) {

		bool color = level == LEVEL_WARN || level == LEVEL_ERROR;

		color = color && lg.color;
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
 * @param safe  True for safe formatted string, false for unsafe
 * @param level Log level
 * @param fmt   Formatted message
 * @param ...   Variable arguments
 */
void _loglv(bool safe, enum log_level level, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vlog(safe, level, fmt, ap);
	va_end(ap);
}


/**
 * Print a DEBUG message to the logging system
 *
 * @param safe  True for safe formatted string, false for unsafe
 * @param fmt   Formatted message
 * @param ...   Variable arguments
 */
void _debug(bool safe, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vlog(safe, LEVEL_DEBUG, fmt, ap);
	va_end(ap);
}


/**
 * Print an INFO message to the logging system
 *
 * @param safe  True for safe formatted string, false for unsafe
 * @param fmt   Formatted message
 * @param ...   Variable arguments
 */
void _info(bool safe, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vlog(safe, LEVEL_INFO, fmt, ap);
	va_end(ap);
}


/**
 * Print a WARNING message to the logging system
 *
 * @param safe  True for safe formatted string, false for unsafe
 * @param fmt   Formatted message
 * @param ...   Variable arguments
 */
void _warning(bool safe, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vlog(safe, LEVEL_WARN, fmt, ap);
	va_end(ap);
}
