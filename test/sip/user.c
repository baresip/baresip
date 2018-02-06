/**
 * @file sip/user.c Mock SIP server -- user handling
 *
 * Copyright (C) 2010 - 2016 Creytiv.com
 */
#include <re.h>
#include "sipsrv.h"


struct user {
	struct le he;
	uint8_t ha1[MD5_SIZE];
	char *name;
};


static void destructor(void *arg)
{
	struct user *usr = arg;

	hash_unlink(&usr->he);
	mem_deref(usr->name);
}


int user_add(struct hash *ht, const char *username, const char *password,
	     const char *realm)
{
	struct user *usr;
	int err;

	usr = mem_zalloc(sizeof(*usr), destructor);
	if (!usr) {
		err = ENOMEM;
		goto out;
	}

	err = str_dup(&usr->name, username);
	if (err) {
		goto out;
	}

	err = md5_printf(usr->ha1, "%s:%s:%s", username, realm, password);
	if (err) {
		goto out;
	}

	hash_append(ht, hash_joaat_str(username), &usr->he, usr);

 out:
	if (err) {
		mem_deref(usr);
	}

	return err;
}


struct user *user_find(struct hash *ht, const struct pl *name)
{
	struct list *lst;
	struct le *le;

	if (!ht || !name)
		return NULL;

	lst = hash_list(ht, hash_joaat((uint8_t *)name->p, name->l));

	for (le=list_head(lst); le; le=le->next) {

		struct user *usr = le->data;

		if (pl_strcmp(name, usr->name))
			continue;

		return usr;
	}

	return NULL;
}


const uint8_t *user_ha1(const struct user *usr)
{
	return usr ? usr->ha1 : NULL;
}
