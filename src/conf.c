/**
 * @file conf.c  Configuration utils
 *
 * Copyright (C) 2010 Creytiv.com
 */
#define _DEFAULT_SOURCE 1
#define _BSD_SOURCE 1
#include <fcntl.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <stdio.h>
#include <sys/stat.h>
#ifdef HAVE_IO_H
#include <io.h>
#endif
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "core.h"


#define DEBUG_MODULE ""
#define DEBUG_LEVEL 0
#include <re_dbg.h>


#ifdef WIN32
#define open _open
#define read _read
#define close _close
#endif


#if defined (WIN32)
#define DIR_SEP "\\"
#else
#define DIR_SEP "/"
#endif


static const char *conf_path = NULL;
static struct conf *conf_obj;


/**
 * Check if a file exists
 *
 * @param path Filename
 *
 * @return True if exist, False if not
 */
bool conf_fileexist(const char *path)
{
	struct stat st;

	if (!path)
		 return false;

	if (stat(path, &st) < 0)
		 return false;

	if ((st.st_mode & S_IFMT) != S_IFREG)
		 return false;

	return true;
}


static void print_populated(const char *what, uint32_t n)
{
	info("Populated %u %s%s\n", n, what, 1==n ? "" : "s");
}


/**
 * Parse a config file, calling handler for each line
 *
 * @param filename Config file
 * @param ch       Line handler
 * @param arg      Handler argument
 *
 * @return 0 if success, otherwise errorcode
 */
int conf_parse(const char *filename, confline_h *ch, void *arg)
{
	struct pl pl, val;
	struct mbuf *mb;
	int err = 0, fd = open(filename, O_RDONLY);
	if (fd < 0)
		return errno;

	mb = mbuf_alloc(1024);
	if (!mb) {
		err = ENOMEM;
		goto out;
	}

	for (;;) {
		uint8_t buf[1024];

		const ssize_t n = read(fd, (void *)buf, sizeof(buf));
		if (n < 0) {
			err = errno;
			break;
		}
		else if (n == 0)
			break;

		err |= mbuf_write_mem(mb, buf, n);
	}

	pl.p = (const char *)mb->buf;
	pl.l = mb->end;

	while (pl.p < ((const char *)mb->buf + mb->end) && !err) {
		const char *lb = pl_strchr(&pl, '\n');

		val.p = pl.p;
		val.l = lb ? (uint32_t)(lb - pl.p) : pl.l;
		pl_advance(&pl, val.l + 1);

		if (!val.l || val.p[0] == '#')
			continue;

		err = ch(&val, arg);
	}

 out:
	mem_deref(mb);
	(void)close(fd);

	return err;
}


/**
 * Set the path to configuration files
 *
 * @param path Configuration path
 */
void conf_path_set(const char *path)
{
	conf_path = path;
}


/**
 * Get the path to configuration files
 *
 * @param path Buffer to write path
 * @param sz   Size of path buffer
 *
 * @return 0 if success, otherwise errorcode
 */
int conf_path_get(char *path, size_t sz)
{
	char buf[FS_PATH_MAX];
	int err;

	/* Use explicit conf path */
	if (conf_path) {
		if (re_snprintf(path, sz, "%s", conf_path) < 0)
			return ENOMEM;
		return 0;
	}

#ifdef CONFIG_PATH
	str_ncpy(buf, CONFIG_PATH, sizeof(buf));
	(void)err;
#else
	err = fs_gethome(buf, sizeof(buf));
	if (err)
		return err;
#endif

	if (re_snprintf(path, sz, "%s" DIR_SEP ".baresip", buf) < 0)
		return ENOMEM;

	return 0;
}


int conf_get_range(const struct conf *conf, const char *name,
		   struct range *rng)
{
	struct pl r, min, max;
	uint32_t v;
	int err;

	err = conf_get(conf, name, &r);
	if (err)
		return err;

	err = re_regex(r.p, r.l, "[0-9]+-[0-9]+", &min, &max);
	if (err) {
		/* fallback to non-range numeric value */
		err = conf_get_u32(conf, name, &v);
		if (err) {
			warning("conf: %s: could not parse range: (%r)\n",
				name, &r);
			return err;
		}

		rng->min = rng->max = v;

		return err;
	}

	rng->min = pl_u32(&min);
	rng->max = pl_u32(&max);

	if (rng->min > rng->max) {
		warning("conf: %s: invalid range (%u - %u)\n",
			name, rng->min, rng->max);
		return EINVAL;
	}

	return 0;
}


int conf_get_csv(const struct conf *conf, const char *name,
		 char *str1, size_t sz1, char *str2, size_t sz2)
{
	struct pl r, pl1, pl2 = pl_null;
	int err;

	err = conf_get(conf, name, &r);
	if (err)
		return err;

	/* note: second value may be quoted */
	err = re_regex(r.p, r.l, "[^,]+,[~]*", &pl1, &pl2);
	if (err)
		return err;

	(void)pl_strcpy(&pl1, str1, sz1);
	if (pl_isset(&pl2))
		(void)pl_strcpy(&pl2, str2, sz2);

	return 0;
}


/**
 * Get the video size of a configuration item
 *
 * @param conf Configuration object
 * @param name Name of config item key
 * @param sz   Returned video size of config item, if present
 *
 * @return 0 if success, otherwise errorcode
 */
int conf_get_vidsz(const struct conf *conf, const char *name, struct vidsz *sz)
{
	struct pl r, w, h;
	int err;

	err = conf_get(conf, name, &r);
	if (err)
		return err;

	w.l = h.l = 0;
	err = re_regex(r.p, r.l, "[0-9]+x[0-9]+", &w, &h);
	if (err)
		return err;

	if (pl_isset(&w) && pl_isset(&h)) {
		sz->w = pl_u32(&w);
		sz->h = pl_u32(&h);
	}

	/* check resolution */
	if (sz->w & 0x1 || sz->h & 0x1) {
		warning("conf: %s: should be multiple of 2 (%u x %u)\n",
			name, sz->w, sz->h);
		return EINVAL;
	}

	return 0;
}


/**
 * Get the socket address of a configuration item
 *
 * @param conf Configuration object
 * @param name Name of config item key
 * @param sa   Returned socket address of config item, if present
 *
 * @return 0 if success, otherwise errorcode
 */
int conf_get_sa(const struct conf *conf, const char *name, struct sa *sa)
{
	struct pl opt;
	int err;

	if (!conf || !name || !sa)
		return EINVAL;

	err = conf_get(conf, name, &opt);
	if (err)
		return err;

	return sa_decode(sa, opt.p, opt.l);
}


int conf_get_float(const struct conf *conf, const char *name, double *val)
{
	struct pl opt;
	int err;

	if (!conf || !name || !val)
		return EINVAL;

	err = conf_get(conf, name, &opt);
	if (err)
		return err;

	*val = pl_float(&opt);

	return 0;
}


/**
 * Configure the system with default settings
 *
 * @return 0 if success, otherwise errorcode
 */
int conf_configure(void)
{
	char path[FS_PATH_MAX], file[FS_PATH_MAX];
	int err;

#if defined (WIN32)
	dbg_init(DBG_INFO, DBG_NONE);
#endif

	err = conf_path_get(path, sizeof(path));
	if (err) {
		warning("conf: could not get config path: %m\n", err);
		return err;
	}

	if (re_snprintf(file, sizeof(file), "%s/config", path) < 0)
		return ENOMEM;

	if (!conf_fileexist(file)) {

		(void)fs_mkdir(path, 0700);

		err = config_write_template(file, conf_config());
		if (err)
			goto out;
	}

	conf_obj = mem_deref(conf_obj);
	err = conf_alloc(&conf_obj, file);
	if (err)
		goto out;

	err = config_parse_conf(conf_config(), conf_obj);
	if (err)
		goto out;

 out:
	return err;
}


/**
 * Load all modules from config file
 *
 * @return 0 if success, otherwise errorcode
 *
 * @note conf_configure must be called first
 */
int conf_modules(void)
{
	int err;

	err = module_init(conf_obj);
	if (err) {
		warning("conf: configure module parse error (%m)\n", err);
		goto out;
	}

	print_populated("audio codec",  list_count(baresip_aucodecl()));
	print_populated("audio filter", list_count(baresip_aufiltl()));
#ifdef USE_VIDEO
	print_populated("video codec",  list_count(baresip_vidcodecl()));
	print_populated("video filter", list_count(baresip_vidfiltl()));
#endif

 out:
	return err;
}


/**
 * Get the current configuration object
 *
 * @return Config object
 *
 * @note It is only available after init and before conf_close()
 */
struct conf *conf_cur(void)
{
	if (!conf_obj) {
		warning("conf: no config object\n");
	}
	return conf_obj;
}


/**
 * Close the current configuration object
 */
void conf_close(void)
{
	conf_obj = mem_deref(conf_obj);
}
