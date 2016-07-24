/**
 * @file src/contact.c  Contacts handling
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <string.h>
#include <re.h>
#include <baresip.h>


enum access {
	ACCESS_UNKNOWN = 0,
	ACCESS_BLOCK,
	ACCESS_ALLOW
};

struct contact {
	struct le le;
	struct le he;          /* hash-element with key 'auri' */
	struct sip_addr addr;
	char *buf;
	enum presence_status status;
	enum access access;
};


static void destructor(void *arg)
{
	struct contact *c = arg;

	hash_unlink(&c->he);
	list_unlink(&c->le);
	mem_deref(c->buf);
}


/**
 * Add a contact
 *
 * @param contacts Contacts container
 * @param contactp Pointer to allocated contact (optional)
 * @param addr     Contact in SIP address format
 *
 * @return 0 if success, otherwise errorcode
 */
int contact_add(struct contacts *contacts,
		struct contact **contactp, const struct pl *addr)
{
	struct contact *c;
	struct pl pl;
	int err;

	if (!contacts)
		return EINVAL;

	c = mem_zalloc(sizeof(*c), destructor);
	if (!c)
		return ENOMEM;

	err = pl_strdup(&c->buf, addr);
	if (err)
		goto out;

	pl_set_str(&pl, c->buf);

	err = sip_addr_decode(&c->addr, &pl);
	if (err) {
		warning("contact: decode error '%r'\n", addr);
		goto out;
	}

	if (0 == msg_param_decode(&c->addr.params, "access", &pl)) {

		if (0 == pl_strcasecmp(&pl, "block")) {
			c->access = ACCESS_BLOCK;
		}
		else if (0 == pl_strcasecmp(&pl, "allow")) {
			c->access = ACCESS_ALLOW;
		}
		else {
			warning("contact: unknown 'access=%r' for '%r'\n",
				&pl, addr);
			err = EINVAL;
			goto out;
		}
	}
	else
		c->access = ACCESS_UNKNOWN;

	c->status = PRESENCE_UNKNOWN;

	list_append(&contacts->cl, &c->le, c);
	hash_append(contacts->cht, hash_joaat_pl(&c->addr.auri), &c->he, c);

 out:
	if (err)
		mem_deref(c);
	else if (contactp)
		*contactp = c;

	return err;
}


/**
 * Get the SIP address of a contact
 *
 * @param c Contact
 *
 * @return SIP Address
 */
struct sip_addr *contact_addr(const struct contact *c)
{
	return c ? (struct sip_addr *)&c->addr : NULL;
}


/**
 * Get the contact string
 *
 * @param c Contact
 *
 * @return Contact string
 */
const char *contact_str(const struct contact *c)
{
	return c ? c->buf : NULL;
}


/**
 * Get the list of contacts
 *
 * @param contacts Contacts container
 *
 * @return List of contacts
 */
struct list *contact_list(const struct contacts *contacts)
{
	if (!contacts)
		return NULL;

	return (struct list *)&contacts->cl;
}


void contact_set_presence(struct contact *c, enum presence_status status)
{
	if (!c)
		return;

	if (c->status != PRESENCE_UNKNOWN && c->status != status) {

		info("<%r> changed status from %s to %s\n", &c->addr.auri,
		     contact_presence_str(c->status),
		     contact_presence_str(status));
	}

	c->status = status;
}


const char *contact_presence_str(enum presence_status status)
{
	switch (status) {

	default:
	case PRESENCE_UNKNOWN: return "\x1b[32mUnknown\x1b[;m";
	case PRESENCE_OPEN:    return "\x1b[32mOnline\x1b[;m";
	case PRESENCE_CLOSED:  return "\x1b[31mOffline\x1b[;m";
	case PRESENCE_BUSY:    return "\x1b[31mBusy\x1b[;m";
	}
}


int contacts_print(struct re_printf *pf, const struct contacts *contacts)
{
	const struct list *lst;
	struct le *le;
	int err;

	if (!contacts)
		return 0;

	lst = contact_list(contacts);

	err = re_hprintf(pf, "\n--- Contacts: (%u) ---\n",
			 list_count(lst));

	for (le = list_head(lst); le && !err; le = le->next) {
		const struct contact *c = le->data;
		const struct sip_addr *addr = &c->addr;

		err = re_hprintf(pf, "%20s  %r <%r>\n",
				 contact_presence_str(c->status),
				 &addr->dname, &addr->auri);
	}

	err |= re_hprintf(pf, "\n");

	return err;
}


/**
 * Initialise the contacts sub-system
 *
 * @param contacts Contacts container
 *
 * @return 0 if success, otherwise errorcode
 */
int contact_init(struct contacts *contacts)
{
	int err = 0;

	if (!contacts)
		return EINVAL;

	memset(contacts, 0, sizeof(*contacts));

	list_init(&contacts->cl);

	err = hash_alloc(&contacts->cht, 32);

	return err;
}


/**
 * @param contacts Contacts container
 *
 * Close the contacts sub-system
 */
void contact_close(struct contacts *contacts)
{
	if (!contacts)
		return;

	hash_clear(contacts->cht);
	contacts->cht = mem_deref(contacts->cht);
	list_flush(&contacts->cl);
}


static bool find_handler(struct le *le, void *arg)
{
	struct contact *c = le->data;

	return 0 == pl_strcmp(&c->addr.auri, arg);
}


/**
 * Lookup a SIP uri in all registered contacts
 *
 * @param contacts Contacts container
 * @param uri      SIP uri to lookup
 *
 * @return Matching contact if found, otherwise NULL
 */
struct contact *contact_find(const struct contacts *contacts, const char *uri)
{
	if (!contacts)
		return NULL;

	return list_ledata(hash_lookup(contacts->cht, hash_joaat_str(uri),
				       find_handler, (void *)uri));
}


/**
 * Check the access parameter of a SIP uri
 *
 * - Matching uri has first presedence
 * - Global <sip:*@*> uri has second presedence
 *
 * @param contacts Contacts container
 * @param uri      SIP uri to check for access
 *
 * @return True if blocked, false if allowed
 */
bool contact_block_access(const struct contacts *contacts, const char *uri)
{
	struct contact *c;

	c = contact_find(contacts, uri);
	if (c && c->access != ACCESS_UNKNOWN)
		return c->access == ACCESS_BLOCK;

	c = contact_find(contacts, "sip:*@*");
	if (c && c->access != ACCESS_UNKNOWN)
		return c->access == ACCESS_BLOCK;

	return false;
}
