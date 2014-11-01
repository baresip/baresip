/**
 * @file src/contact.c  Contacts handling
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <re.h>
#include <baresip.h>


struct contact {
	struct le le;
	struct sip_addr addr;
	char *buf;
	enum presence_status status;
};

static struct list cl;


static void destructor(void *arg)
{
	struct contact *c = arg;

	list_unlink(&c->le);
	mem_deref(c->buf);
}


/**
 * Add a contact
 *
 * @param contactp Pointer to allocated contact (optional)
 * @param addr     Contact in SIP address format
 *
 * @return 0 if success, otherwise errorcode
 */
int contact_add(struct contact **contactp, const struct pl *addr)
{
	struct contact *c;
	struct pl pl;
	int err;

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

	c->status = PRESENCE_UNKNOWN;

	list_append(&cl, &c->le, c);

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
 * @return List of contacts
 */
struct list *contact_list(void)
{
	return &cl;
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


int contacts_print(struct re_printf *pf, void *unused)
{
	struct le *le;
	int err;

	(void)unused;

	err = re_hprintf(pf, "\n--- Contacts: (%u) ---\n",
			 list_count(contact_list()));

	for (le = list_head(contact_list()); le && !err; le = le->next) {
		const struct contact *c = le->data;
		const struct sip_addr *addr = &c->addr;

		err = re_hprintf(pf, "%20s  %r <%r>\n",
				 contact_presence_str(c->status),
				 &addr->dname, &addr->auri);
	}

	err |= re_hprintf(pf, "\n");

	return err;
}
