/**
 * @file src/module.c Module loading
 *
 * Copyright (C) 2010 Alfred E. Heggestad
 */
#include <re.h>
#include <baresip.h>
#include "core.h"


static struct {
	char *name;
	char *reason;
	char *version;
} notfound[] =  {
	/* moved */
	{"b2bua", "moved to baresip-apps", ""},
	{"auloop", "moved to baresip-apps", ""},
	{"vidloop", "moved to baresip-apps", ""},
	{"mc", "moved to baresip-apps", ""},
	{"mpa", "moved to baresip-apps", "v4.2.0"},
	{"ebuacip", "moved to baresip-apps", "v4.0.0"},
	/* replaced */
	{"winwave", "replaced by wasapi module", "v3.18.0"},
	{"zrtp", "use gzrtp instead", "v2.9.0"},
	{"x11grab", "use avformat.so instead", "v1.x"},
	{"celt", "module deleted, use opus.so instead", "v0.4.12"},
	{"v4l", "deprecated module, use v4l2.so instead", "v0.6.2"},
	/* deleted */
	{"directfb", "deprecated module", "v4.10.0"},
	{"g726", "deprecated module", "v4.8.0"},
	{"webrtc_aecm", "removed module", "v4.2.0"},
	{"sndio", "deprecated module", "v3.0.0"},
	{"i2s", "deprecated module", "v2.9.0"},
	{"omx", "deprecated module", ""},
	{"gsm", "deprecated module", ""},
	{"gst_video", "deprecated module", "v2.x"},
	{"speex_pp", "deprecated module", "v1.x"},
	{"rst", "deprecated module", "v1.x"},
	{"ilbc", "deprecated module", "v1.x"},
	{"oss", "deprecated module", ""},
	{"cairo", "deprecated module", ""},
	{"opengl", "deprecated module", ""},
	{"isac", "deprecated module", ""},
	{"qtcapture", "deprecated module", ""},
	{"opengles", "deprecated module (not working)", ""},
	{"avahi", "deprecated module", ""},
	{"dtmfio", "deprecated module", ""},
	{"sdl", "deprecated module", "v0.6.2"},
};


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
	/* Try static first; if a static module is available
		assume that it is "sound" and that the call to
		mod_add (which initializes the module) should
		succeed */
	pl_strcpy(name, namestr, sizeof(namestr));

	if (mod_find(namestr)) {
		info("static module already loaded: %r\n", name);
		return EALREADY;
	}

	const struct mod_export* mex = lookup_static_module(name);
	if (mex) {
		err = mod_add(&m, mex);
		goto out;
	}
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
		for (size_t i = 0; i < RE_ARRAY_SIZE(notfound); i++) {
			if (0 == pl_strcasecmp(name, notfound[i].name)) {
				warning("\t%s (since %s)\n",
					notfound[i].reason,
					notfound[i].version);
				break;
			}
		}
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
 * Pre-load a module. First check the current working directory
 * then fall back to the configured module path
 *
 * @param module Module name including extension
 *
 * @return 0 if success, otherwise errorcode
 */
int module_preload(const char *module)
{
	struct pl path, name;
	int err;

	if (!module)
		return EINVAL;

	pl_set_str(&path, ".");
	pl_set_str(&name, module);

	char *file = NULL;
	err = re_sdprintf(&file, "%r/%r", &path, &name);
	if (err)
		return err;

	if (! fs_isfile(file)) {
		const struct conf *conf = conf_cur();
		conf_get(conf, "module_path", &path);
	}

	mem_deref(file);

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
