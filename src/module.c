/**
 * @file src/module.c Module loading
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <re.h>
#include <baresip.h>
#include "core.h"


/*
 * Append module extension, if not exist
 *
 * input:    foobar
 * output:   foobar.so
 *
 */
static void append_extension(char *buf, size_t sz, const char *name)
{
	if (0 == re_regex(name, str_len(name), "[^.]+"MOD_EXT, NULL)) {

		str_ncpy(buf, name, sz);
	}
	else {
		re_snprintf(buf, sz, "%s"MOD_EXT, name);
	}
}


#ifdef STATIC

/* Declared in static.c */
extern const struct mod_export *mod_table[];

static const struct mod_export *lookup_static_module(const struct pl *pl)
{
	struct pl name;
	uint32_t i;

	if (re_regex(pl->p, pl->l, "[^.]+.[^]*", &name, NULL))
		name = *pl;

	for (i=0; ; i++) {
		const struct mod_export *me = mod_table[i];
		if (!me)
			return NULL;
		if (0 == pl_strcasecmp(&name, me->name))
			return me;
	}

	return NULL;
}
#endif


static int load_module(struct mod **modp, const struct pl *modpath,
		       const struct pl *name)
{
	char file[FS_PATH_MAX];
	char namestr[256];
	struct mod *m = NULL;
	int err = 0;

	if (!name)
		return EINVAL;

#ifdef STATIC
	/* Try static first */
	pl_strcpy(name, namestr, sizeof(namestr));

	if (mod_find(namestr)) {
		info("static module already loaded: %r\n", name);
		return EALREADY;
	}

	err = mod_add(&m, lookup_static_module(name));
	if (!err)
		goto out;
#else
	(void)namestr;
#endif

	/* Then dynamic */
	if (re_snprintf(file, sizeof(file), "%r/%r", modpath, name) < 0) {
		err = ENOMEM;
		goto out;
	}
	err = mod_load(&m, file);
	if (err)
		goto out;

 out:
	if (err) {
		warning("module %r: %m\n", name, err);
	}
	else if (modp)
		*modp = m;

	return err;
}


static int module_handler(const struct pl *val, void *arg)
{
	(void)load_module(NULL, arg, val);
	return 0;
}


static int module_tmp_handler(const struct pl *val, void *arg)
{
	struct mod *mod = NULL;
	(void)load_module(&mod, arg, val);
	mem_deref(mod);
	return 0;
}


static int module_app_handler(const struct pl *val, void *arg)
{
	struct mod *mod = NULL;
	const struct mod_export *me;

	debug("module: loading app %r\n", val);

	if (load_module(&mod, arg, val)) {
		return 0;
	}

	me = mod_export(mod);
	if (0 != str_casecmp(me->type, "application")) {
		warning("module_app %r should be type application (%s)\n",
			val, me->type);
	}

	return 0;
}


int module_init(const struct conf *conf)
{
	struct pl path;
	int err;

	if (!conf)
		return EINVAL;

	if (conf_get(conf, "module_path", &path))
		pl_set_str(&path, ".");

	err = conf_apply(conf, "module", module_handler, &path);
	if (err)
		return err;

	err = conf_apply(conf, "module_tmp", module_tmp_handler, &path);
	if (err)
		return err;

	err = conf_apply(conf, "module_app", module_app_handler, &path);
	if (err)
		return err;

	return 0;
}


/**
 * Unload all application modules in reverse order
 */
void module_app_unload(void)
{
	struct le *le = list_tail(mod_list());

	/* unload in reverse order */
	while (le) {
		struct mod *mod = le->data;
		const struct mod_export *me = mod_export(mod);

		le = le->prev;

		if (me && 0 == str_casecmp(me->type, "application")) {
			debug("module: unloading app %s\n", me->name);
			mem_deref(mod);
		}
	}
}


/**
 * Pre-load a module from the current working directory
 *
 * @param module Module name including extension
 *
 * @return 0 if success, otherwise errorcode
 */
int module_preload(const char *module)
{
	struct pl path, name;

	if (!module)
		return EINVAL;

	pl_set_str(&path, ".");
	pl_set_str(&name, module);

	return load_module(NULL, &path, &name);
}


/**
 * Load a module by name or by filename
 *
 * @param path Module path
 * @param name Module name incl/excl extension, excluding module path
 *
 * @return 0 if success, otherwise errorcode
 *
 * example:    "foo"
 * example:    "foo.so"
 */
int module_load(const char *path, const char *name)
{
	char filename[256];
	struct pl pl_path, pl_name;
	int err;

	if (!str_isset(name))
		return EINVAL;

	append_extension(filename, sizeof(filename), name);

	pl_set_str(&pl_path, path);
	pl_set_str(&pl_name, filename);

	err = load_module(NULL, &pl_path, &pl_name);

	return err;
}


/**
 * Unload a module by name or by filename
 *
 * @param name module name incl/excl extension, excluding module path
 *
 * example:   "foo"
 * example:   "foo.so"
 */
void module_unload(const char *name)
{
	char filename[256];
	struct mod *mod;

	if (!str_isset(name))
		return;

	append_extension(filename, sizeof(filename), name);

	mod = mod_find(filename);
	if (mod) {
		info("unloading module: %s\n", filename);
		mem_deref(mod);
		return;
	}

	info("ERROR: Module %s is not currently loaded\n", name);
}
