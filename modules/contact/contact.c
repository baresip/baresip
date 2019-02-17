/**
 * @file modules/contact/contact.c  Contacts module
 *
 * Copyright (C) 2010 - 2015 Creytiv.com
 */
#include <string.h>
#include <re.h>
#include <baresip.h>


/**
 * @defgroup contact contact
 *
 * Contact module reading contacts from a file
 *
 * - read contact entries from ~/.baresip/contacts
 * - populate local database of contacts
 */


static int confline_handler(const struct pl *addr, void *arg)
{
	struct contacts *contacts = arg;

	return contact_add(contacts, NULL, addr);
}


static int print_contacts(struct re_printf *pf, void *unused)
{
	(void)unused;

	return contacts_print(pf, baresip_contacts());
}


static int save_current(const struct contact *cnt)
{
	char path[256] = "", file[256] = "";
	FILE *f = NULL;
	int err;

	err = conf_path_get(path, sizeof(path));
	if (err)
		return err;

	if (re_snprintf(file, sizeof(file), "%s/current_contact", path) < 0)
		return ENOMEM;

	f = fopen(file, "w");
	if (!f)
		return errno;

	if (re_fprintf(f, "%s", contact_uri(cnt)) < 0) {
		err = errno;
		goto out;
	}

 out:
	if (f)
		(void)fclose(f);

	return err;
}


static void send_resp_handler(int err, const struct sip_msg *msg, void *arg)
{
	(void)arg;

	if (err) {
		(void)re_fprintf(stderr, " \x1b[31m%m\x1b[;m\n", err);
		return;
	}

	if (msg->scode >= 300) {
		(void)re_fprintf(stderr, " \x1b[31m%u %r\x1b[;m\n",
				 msg->scode, &msg->reason);
	}
}


static int cmd_dial_contact(struct re_printf *pf, void *arg)
{
	struct contact *cnt;
	const char *uri;
	int err = 0;
	(void)arg;

	cnt = contacts_current(baresip_contacts());
	if (!cnt) {
		return re_hprintf(pf, "contact: current contact not set\n");
	}

	uri = contact_uri(cnt);

	err = ua_connect(uag_current(), NULL, NULL, uri, VIDMODE_ON);
	if (err) {
		warning("contact: ua_connect(%s) failed: %m\n",
			uri, err);
	}

	return 0;
}


static int cmd_message(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	struct contact *cnt;
	const char *uri;
	int err = 0;

	cnt = contacts_current(baresip_contacts());
	if (!cnt) {
		return re_hprintf(pf, "contact: current contact not set\n");
	}

	uri = contact_uri(cnt);

	err = message_send(uag_current(), uri, carg->prm,
			   send_resp_handler, NULL);
	if (err) {
		(void)re_hprintf(pf, "contact: message_send(%s) failed (%m)\n",
				 uri, err);
	}

	return err;
}


static int load_current_contact(struct contacts *contacts, const char *path)
{
	char file[256] = "";
	char buf[1024];
	struct contact *cnt = NULL;
	struct sip_addr addr;
	struct pl pl;
	FILE *f = NULL;
	int err = 0;

	if (re_snprintf(file, sizeof(file), "%s/current_contact", path) < 0)
		return ENOMEM;

	if (conf_fileexist(file)) {

		f = fopen(file, "r");
		if (!f)
			return errno;

		if (!fgets(buf, (int)sizeof(buf), f)) {
			err = errno;
			goto out;
		}

		pl_set_str(&pl, buf);
		if (0 == sip_addr_decode(&addr, &pl))
			pl_strcpy(&addr.auri, buf, sizeof(buf));

		cnt = contact_find(contacts, buf);
		if (!cnt) {
			info("contact from disk not found (%s)\n", buf);
		}
	}

	if (!cnt) {
		cnt = list_ledata(list_head(contact_list(contacts)));

		err = save_current(cnt);
		if (err)
			goto out;
	}

	if (cnt)
		contacts_set_current(contacts, cnt);

 out:
	if (f)
		(void)fclose(f);

	return err;
}


static int cycle_current(struct re_printf *pf, bool next)
{
	struct contacts *contacts = baresip_contacts();
	struct contact *cnt;
	struct le *le;
	int err;

	cnt = contacts_current(contacts);
	if (cnt) {
		le = contact_le(cnt);

		if (next)
			le = le->next ? le->next : le;
		else
			le = le->prev ? le->prev : le;
	}
	else {
		/* No current contact, set the first one */
		le = list_head(contact_list(contacts));
		if (!le)
			return re_hprintf(pf, "(no contacts)\n");
	}

	cnt = list_ledata(le);

	contacts_set_current(contacts, cnt);

	re_hprintf(pf, "Current contact: %H\n", contact_print, cnt);

	err = save_current(cnt);
	if (err) {
		warning("contact: failed to save"
			" current contact (%m)\n", err);
	}

	return 0;
}


static int cmd_current_prev(struct re_printf *pf, void *arg)
{
	(void)arg;

	return cycle_current(pf, false);
}


static int cmd_current_next(struct re_printf *pf, void *arg)
{
	(void)arg;

	return cycle_current(pf, true);
}


static const struct cmd cmdv[] = {
{"contacts",     'C',        0, "List contacts",          print_contacts    },
{"dialcontact",  'D',        0, "Dial current contact",   cmd_dial_contact  },
{"message",      'M',  CMD_PRM, "Message current contact",cmd_message       },
{"contact_prev", '<',        0, "Set previous contact",   cmd_current_prev  },
{"contact_next", '>',        0, "Set next contact",       cmd_current_next  },
};


static int write_template(const char *file)
{
	const char *user, *domain;
	FILE *f = NULL;

	info("contact: creating contacts template %s\n", file);

	f = fopen(file, "w");
	if (!f)
		return errno;

	user = sys_username();
	if (!user)
		user = "user";
	domain = net_domain(baresip_network());
	if (!domain)
		domain = "domain";

	(void)re_fprintf(f,
			 "#\n"
			 "# SIP contacts\n"
			 "#\n"
			 "# Displayname <sip:user@domain>;addr-params\n"
			 "#\n"
			 "#  addr-params:\n"
			 "#    ;presence={none,p2p}\n"
			 "#    ;access={allow,block}\n"
			 "#\n"
			 "\n"
			 "\n"
			 "\"Echo Server\" <sip:echo@creytiv.com>\n"
			 "\"%s\" <sip:%s@%s>;presence=p2p\n"
			 "\n"
			 "# Access rules\n"
			 "#\"Catch All\" <sip:*@*>;access=block\n"
			 "\"Good Friend\" <sip:good@friend.com>;access=allow\n"
			 "\n"
			 ,
			 user, user, domain);

	if (f)
		(void)fclose(f);

	return 0;
}


static int module_init(void)
{
	struct contacts *contacts = baresip_contacts();
	char path[256] = "", file[256] = "";
	int err;

	err = conf_path_get(path, sizeof(path));
	if (err)
		return err;

	if (re_snprintf(file, sizeof(file), "%s/contacts", path) < 0)
		return ENOMEM;

	if (!conf_fileexist(file)) {

		(void)fs_mkdir(path, 0700);

		err = write_template(file);
		if (err)
			return err;
	}

	err = conf_parse(file, confline_handler, contacts);
	if (err)
		return err;

	err = cmd_register(baresip_commands(), cmdv, ARRAY_SIZE(cmdv));
	if (err)
		return err;

	info("Populated %u contacts\n",
	     list_count(contact_list(contacts)));

	/* Load current contact after list was populated */
	if (!list_isempty(contact_list(contacts))) {

		err = load_current_contact(contacts, path);
		if (err) {
			warning("could not load current contact (%m)\n", err);
			err = 0;
		}
	}

	return err;
}


static int module_close(void)
{
	cmd_unregister(baresip_commands(), cmdv);
	list_flush(contact_list(baresip_contacts()));

	return 0;
}


const struct mod_export DECL_EXPORTS(contact) = {
	"contact",
	"application",
	module_init,
	module_close
};
