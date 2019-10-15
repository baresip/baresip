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
	char *uri;
	enum presence_status status;
	enum access access;
};


struct contacts {
	struct list cl;
	struct hash *cht;
	struct contact *cur;
	bool enable_presence;

	contact_update_h *handler;
	void *handler_arg;
};


static void destructor(void *arg)
{
	struct contact *c = arg;

	hash_unlink(&c->he);
	list_unlink(&c->le);
	mem_deref(c->buf);
	mem_deref(c->uri);
}


static void contacts_destructor(void *data)
{
	struct contacts *contacts = data;

	mem_deref(contacts->cur);

	hash_clear(contacts->cht);
	mem_deref(contacts->cht);
	list_flush(&contacts->cl);
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

	err = pl_strdup(&c->uri, &c->addr.auri);
	if (err)
		goto out;

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

	if (contacts->handler)
		contacts->handler(c, false, contacts->handler_arg);

 out:
	if (err)
		mem_deref(c);
	else if (contactp)
		*contactp = c;

	return err;
}


/**
 * Remove a contact
 *
 * @param contacts Contacts container
 * @param contact  Contact to be removed
 */
void contact_remove(struct contacts *contacts, struct contact *contact)
{
	if (!contacts || !contact)
		return;

	if (contacts->handler)
		contacts->handler(contact, true, contacts->handler_arg);

	hash_unlink(&contact->he);
	list_unlink(&contact->le);

	if (contacts->cur == contact)
		contacts->cur = mem_deref(contacts->cur);

	mem_deref(contact);
}


/**
 * Set the contacts update handler
 *
 * @param contacts Contacts container
 * @param updateh  Update handler
 * @param arg      Handler argument
 */
void contact_set_update_handler(struct contacts *contacts,
				contact_update_h *updateh, void *arg)
{
	if (!contacts)
		return;

	contacts->handler = updateh;
	contacts->handler_arg = arg;
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
 * Get the SIP uri of a contact
 *
 * @param c Contact
 *
 * @return SIP uri
 */
const char *contact_uri(const struct contact *c)
{
	return c ? c->uri : NULL;
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


/**
 * Set the presence status for a contact
 *
 * @param c      Contact
 * @param status Presence status
 */
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


/**
 * Get the presence status for a contact
 *
 * @param c Contact
 *
 * @return Presence status
 */
enum presence_status contact_presence(const struct contact *c)
{
	if (!c)
		return PRESENCE_UNKNOWN;

	return c->status;
}


/**
 * Get the presence status string
 *
 * @param status Presence status
 *
 * @return Presence status string
 */
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


/**
 * Print a contact
 *
 * @param pf  Print function
 * @param cnt Contact to print
 *
 * @return 0 if success, otherwise errorcode
 */
int contact_print(struct re_printf *pf, const struct contact *cnt)
{
	if (!cnt)
		return 0;

	return re_hprintf(pf, "%r <%r>", &cnt->addr.dname, &cnt->addr.auri);
}


/**
 * Print all contacts
 *
 * @param pf       Print function
 * @param contacts Contacts container
 *
 * @return 0 if success, otherwise errorcode
 */
int contacts_print(struct re_printf *pf, const struct contacts *contacts)
{
	const struct list *lst;
	struct le *le;
	int err;

	if (!contacts)
		return 0;

	lst = contact_list(contacts);

	err = re_hprintf(pf, "\n--- Contacts (%u) ---\n",
			 list_count(lst));

	for (le = list_head(lst); le && !err; le = le->next) {
		const struct contact *c = le->data;

		err = re_hprintf(pf, "%s ", c == contacts->cur ? ">" : " ");

		if (contacts->enable_presence) {
			err |= re_hprintf(pf, "%20s ",
					  contact_presence_str(c->status));
		}

		err |= re_hprintf(pf, "%H\n", contact_print, c);
	}

	err |= re_hprintf(pf, "\n");

	return err;
}


/**
 * Initialise the contacts sub-system
 *
 * @param contactsp Pointer to allocated contacts container
 *
 * @return 0 if success, otherwise errorcode
 */
int contact_init(struct contacts **contactsp)
{
	struct contacts *contacts;
	int err = 0;

	if (!contactsp)
		return EINVAL;

	contacts = mem_zalloc(sizeof(*contacts), contacts_destructor);
	if (!contacts)
		return ENOMEM;

	list_init(&contacts->cl);

	err = hash_alloc(&contacts->cht, 32);
	if (err)
		goto out;

 out:
	if (err)
		mem_deref(contacts);
	else
		*contactsp = contacts;

	return err;
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


/**
 * Set the current contact
 *
 * @param contacts Contacts container
 * @param cnt      Contact to be set as current
 */
void contacts_set_current(struct contacts *contacts, struct contact *cnt)
{
	if (!contacts || !cnt)
		return;

	mem_deref(contacts->cur);
	contacts->cur = mem_ref(cnt);
}


/**
 * Get the current contact
 *
 * @param contacts Contacts container
 *
 * @return Current contact, or NULL if no current contact
 */
struct contact *contacts_current(const struct contacts *contacts)
{
	if (!contacts)
		return NULL;

	return contacts->cur;
}


/**
 * Get the linked list element from a contact
 *
 * @param cnt Contact
 *
 * @return Linked-list element
 */
struct le *contact_le(struct contact *cnt)
{
	return cnt ? &cnt->le : NULL;
}


/**
 * Enable or disable presence
 *
 * @param contacts Contacts container
 * @param enabled  True to enable presence, false to disable
 */
void contacts_enable_presence(struct contacts *contacts, bool enabled)
{
	if (!contacts)
		return;

	contacts->enable_presence = enabled;
}
