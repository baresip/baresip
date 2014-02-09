/**
 * @file modules/contact/contact.c  Contacts module
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <string.h>
#include <re.h>
#include <baresip.h>


/*
 * Contact module
 *
 * - read contact entries from ~/.baresip/contacts
 * - populate local database of contacts
 */


static const char *chat_peer;         /**< Selected chat peer             */
static char cmd_desc[128] = "Send MESSAGE to peer";


static int confline_handler(const struct pl *addr)
{
	return contact_add(NULL, addr);
}


static int cmd_contact(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	struct contact *cnt = NULL;
	struct pl dname, user, pl;
	struct le *le;
	int err = 0;

	pl_set_str(&pl, carg->prm);

	dname.l = user.l = pl.l;

	err |= re_hprintf(pf, "\n");

	for (le = list_head(contact_list()); le; le = le->next) {

		struct contact *c = le->data;

		dname.p = contact_addr(c)->dname.p;
		user.p  = contact_addr(c)->uri.user.p;

		/* if displayname is set, try to match the displayname
		 * otherwise we try to match the username only
		 */
		if (dname.p) {

			if (0 == pl_casecmp(&dname, &pl)) {
				err |= re_hprintf(pf, "%s\n", contact_str(c));
				cnt = c;
			}
		}
		else if (user.p) {

			if (0 == pl_casecmp(&user, &pl)) {
				err |= re_hprintf(pf, "%s\n", contact_str(c));
				cnt = c;
			}
		}
	}

	if (!cnt)
		err |= re_hprintf(pf, "(no matches)\n");

	if (carg->complete && cnt) {

		switch (carg->key) {

		case '/':
			err = ua_connect(uag_current(), NULL, NULL,
					 contact_str(cnt), NULL, VIDMODE_ON);
			if (err) {
				warning("contact: ua_connect failed: %m\n",
					err);
			}
			break;

		case '=':
			chat_peer = contact_str(cnt);
			(void)re_hprintf(pf, "Selected chat peer: %s\n",
					 chat_peer);
			re_snprintf(cmd_desc, sizeof(cmd_desc),
				    "Send MESSAGE to %s", chat_peer);
			break;

		default:
			break;
		}
	}

	return err;
}


static int cmd_message(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	int err;

	(void)pf;

	err = message_send(uag_current(), chat_peer, carg->prm);
	if (err) {
		(void)re_hprintf(pf, "chat: ua_im_send() failed (%m)\n", err);
	}

	return err;
}


static const struct cmd cmdv[] = {
	{'/', CMD_IPRM, "Dial from contacts",       cmd_contact          },
	{'=', CMD_IPRM, "Select chat peer",         cmd_contact          },
	{'C',        0, "List contacts",            contacts_print       },
	{'-',  CMD_PRM, cmd_desc,                   cmd_message          },
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
	domain = net_domain();
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
			 "#\n"
			 "\n"
			 "\"Echo Server\" <sip:echo@creytiv.com>\n"
			 "\"%s\" <sip:%s@%s>;presence=p2p\n",
			 user, user, domain);

	if (f)
		(void)fclose(f);

	return 0;
}


static int module_init(void)
{
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

	err = conf_parse(file, confline_handler);
	if (err)
		return err;

	err = cmd_register(cmdv, ARRAY_SIZE(cmdv));
	if (err)
		return err;

	info("Populated %u contacts\n", list_count(contact_list()));

	return err;
}


static int module_close(void)
{
	cmd_unregister(cmdv);
	list_flush(contact_list());

	return 0;
}


const struct mod_export DECL_EXPORTS(contact) = {
	"contact",
	"application",
	module_init,
	module_close
};
