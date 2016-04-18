/**
 * @file modules/uuid/uuid.c  Generate and load UUID
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <re.h>
#include <baresip.h>


/**
 * @defgroup uuid uuid
 *
 * UUID generator and loader
 */


enum { UUID_LEN = 36 };


static int generate_random_uuid(FILE *f)
{
	if (re_fprintf(f, "%08x-%04x-%04x-%04x-%08x%04x",
		       rand_u32(), rand_u16(), rand_u16(), rand_u16(),
		       rand_u32(), rand_u16()) != UUID_LEN)
		return ENOMEM;

	return 0;
}


static int uuid_init(const char *file)
{
	FILE *f = NULL;
	int err = 0;

	f = fopen(file, "r");
	if (f) {
		err = 0;
		goto out;
	}

	f = fopen(file, "w");
	if (!f) {
		err = errno;
		warning("uuid: fopen() %s (%m)\n", file, err);
		goto out;
	}

	err = generate_random_uuid(f);
	if (err) {
		warning("uuid: generate random UUID failed (%m)\n", err);
		goto out;
	}

	info("uuid: generated new UUID in %s\n", file);

 out:
	if (f)
		fclose(f);

	return err;
}


static int uuid_load(const char *file, char *uuid, size_t sz)
{
	FILE *f = NULL;
	int err = 0;

	f = fopen(file, "r");
	if (!f)
		return errno;

	if (!fgets(uuid, (int)sz, f))
		err = errno;

	(void)fclose(f);

	debug("uuid: loaded UUID %s from file %s\n", uuid, file);

	return err;
}


static int module_init(void)
{
	struct config *cfg = conf_config();
	char path[256];
	int err = 0;

	err = conf_path_get(path, sizeof(path));
	if (err)
		return err;

	strncat(path, "/uuid", sizeof(path) - strlen(path) - 1);

	err = uuid_init(path);
	if (err)
		return err;

	err = uuid_load(path, cfg->sip.uuid, sizeof(cfg->sip.uuid));
	if (err)
		return err;

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(uuid) = {
	"uuid",
	NULL,
	module_init,
	NULL
};
