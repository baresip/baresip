/**
 * @file pipewire.c  Pipewire sound driver
 *
 * Copyright (C) 2023 Commend.com - c.spielberger@commend.com
 */

#include <string.h>
#include <errno.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <spa/param/audio/raw.h>
#include <pipewire/pipewire.h>

#include "pipewire.h"

/**
 * @defgroup pipewire pipewire
 *
 * Audio driver module for Pipewire
 *
 */

enum {
	RECONN_DELAY  = 1500,
	DEV_HASH_SIZE = 16,
};


struct pw_dev {
	struct le he;

	char *node_name;
	uint32_t id;
};


struct pw_stat {
	struct pw_thread_loop *loop;
	struct pw_context *context;
	struct pw_core *core;
	struct pw_registry *registry;
	struct spa_hook registry_listener;

	struct auplay *auplay;
	struct ausrc *ausrc;
	struct hash *devices;
};


static struct pw_stat *d = NULL;


static void destructor(void *arg)
{
	struct pw_stat *pw = arg;

	mem_deref(pw->auplay);
	mem_deref(pw->ausrc);
	hash_flush(pw->devices);
	mem_deref(pw->devices);

	if (pw->core)
		pw_core_disconnect(pw->core);

	if (pw->context)
		pw_context_destroy(pw->context);

	if (pw->loop) {
		pw_thread_loop_stop(pw->loop);
		pw_thread_loop_destroy(pw->loop);
	}
}


static void pw_dev_destructor(void *arg)
{
	struct pw_dev *pwd = arg;

	mem_deref(pwd->node_name);
}


static int pw_dev_add(uint32_t id, const char *node_name)
{
	struct pw_dev *pwd;
	int err;

	pwd = mem_zalloc(sizeof(*pwd), pw_dev_destructor);
	if (!pwd)
		return ENOMEM;

	pwd->id = id;
	err = str_dup(&pwd->node_name, node_name);
	if (err) {
		mem_deref(pwd);
		return ENOMEM;
	}

	hash_append(d->devices, hash_joaat_str(node_name), &pwd->he, pwd);
	return 0;
}


static void registry_event_global(void *arg, uint32_t id,
		uint32_t permissions, const char *type, uint32_t version,
		const struct spa_dict *props)
{
	struct pw_stat *pw = arg;
	const char *media_class;
	const char *node_name;
	(void)permissions;
	(void)version;

	if (str_cmp(type, PW_TYPE_INTERFACE_Node))
		return;

	media_class = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);
	node_name = spa_dict_lookup(props, PW_KEY_NODE_NAME);
	if (!str_cmp(media_class, "Audio/Source") && str_isset(node_name)) {
		debug("pipewire: adding (%u) %s: \"%s\"\n",
		      id, media_class, node_name);
		mediadev_add(&pw->ausrc->dev_list, node_name);
		(void)pw_dev_add(id, node_name);
	}

	if (!str_cmp(media_class, "Audio/Sink") && str_isset(node_name)) {
		debug("pipewire: adding (%u) %s: \"%s\"\n",
		      id, media_class, node_name);
		mediadev_add(&pw->auplay->dev_list, node_name);
		(void)pw_dev_add(id, node_name);
	}
}


static const struct pw_registry_events registry_events = {
	PW_VERSION_REGISTRY_EVENTS,
	.global = registry_event_global,
};


struct pwd_cmp {
	const char *node_name;
};


static bool pw_dev_cmp(struct le *le, void *arg)
{
	const struct pwd_cmp *cmp = arg;
	const struct pw_dev *pwd = le->data;

	return !str_cmp(pwd->node_name, cmp->node_name);
}


int pw_device_id(const char *node_name)
{
	struct le *le;
	struct pw_dev *pwd;
	struct pwd_cmp cmp;

	cmp.node_name = node_name;

	le = hash_lookup(d->devices, hash_joaat_str(node_name),
			  pw_dev_cmp, &cmp);

	if (!le || !le->data)
		return PW_ID_ANY;

	pwd = le->data;
	return pwd->id;
}


static struct pw_stat *pw_stat_alloc(void)
{
	struct pw_stat *pw;
	int err;

	pw = mem_zalloc(sizeof(*pw), destructor);

	pw->loop = pw_thread_loop_new("baresip pipewire", NULL);
	if (!pw->loop)
		goto errout;

	pw_thread_loop_lock(pw->loop);
	err = pw_thread_loop_start(pw->loop);
	if (err)
		goto errout;

	pw->context = pw_context_new(pw_thread_loop_get_loop(pw->loop),
				     NULL /* properties */,
				     0 /* user_data size */);
	if (!pw->context)
		goto errout;

	pw->core = pw_context_connect(pw->context,
				      NULL /* properties */,
				      0 /* user_data size */);
	if (!pw->core)
		goto errout;

	pw_thread_loop_unlock(pw->loop);
	info("pipewire: connected to pipewire\n");
	return pw;

errout:
	if (pw->loop)
		pw_thread_loop_unlock(pw->loop);

	warning("pipewire: could not connect to pipewire\n");
	mem_deref(pw);
	return NULL;
}


static int pw_start_registry_scan(struct pw_stat *pw)
{
	int err;

	pw_thread_loop_lock (pw_loop_instance());
	pw->registry = pw_core_get_registry(pw->core, PW_VERSION_REGISTRY,
					0 /* user_data size */);

	if (!pw->registry)
		return errno;

	err  = hash_alloc(&pw->devices, DEV_HASH_SIZE);
	if (err)
		return err;

	spa_zero(pw->registry_listener);
	pw_registry_add_listener(pw->registry, &pw->registry_listener,
				 &registry_events, pw);
	pw_thread_loop_unlock(pw_loop_instance());
	return 0;
}


struct pw_core *pw_core_instance(void)
{
	if (!d)
		return NULL;

	return d->core;
}


struct pw_thread_loop *pw_loop_instance(void)
{
	if (!d)
		return NULL;

	return d->loop;
}


int aufmt_to_pw_format(enum aufmt fmt)
{
	switch (fmt) {
		case AUFMT_S16LE:  return SPA_AUDIO_FORMAT_S16_LE;
		case AUFMT_FLOAT:  return SPA_AUDIO_FORMAT_F32;
		default: return 0;
	}
}


static int module_init(void)
{
	int err = 0;

	pw_init(NULL, NULL);
	setvbuf(stderr, NULL, _IONBF, 0);
	info("pipewire: headers %s library %s \n",
	     pw_get_headers_version(), pw_get_library_version());

	d = pw_stat_alloc();
	if (!d)
		return errno;

	err  = auplay_register(&d->auplay, baresip_auplayl(),
			       "pipewire", pw_playback_alloc);
	err |= ausrc_register(&d->ausrc, baresip_ausrcl(),
			      "pipewire", pw_capture_alloc);

	err |= pw_start_registry_scan(d);
	return err;
}


static int module_close(void)
{
	d = mem_deref(d);
	pw_deinit();
	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(pipewire) = {
	"pipewire",
	"audio",
	module_init,
	module_close,
};
