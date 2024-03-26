/**
 * @file dialog.c Message Waiting Indication (RFC 3842)
 *
 * Copyright (C) 2024 Viktor Litvinov
 */
#include <string.h>
#include <re.h>
#include <baresip.h>


/**
 * @defgroup dialog dialog
 *
 * Dialog info subscriptions
 *
 */


struct dialog {
	struct le le;
	struct sipsub *sub;
	struct ua *ua;
	struct contact *contact;
	struct tmr tmr;
	bool shutdown;
};

static struct list dialogl;


static void tmr_handler(void *arg);


static void destructor(void *arg)
{
	struct dialog *dialog = arg;

	tmr_cancel(&dialog->tmr);
	list_unlink(&dialog->le);
	mem_deref(dialog->sub);
	mem_deref(dialog->ua);
	mem_deref(dialog->contact);
}


static void deref_handler(void *arg)
{
	struct dialog *dialog = arg;
	mem_deref(dialog);
}


static int auth_handler(char **username, char **password,
			const char *realm, void *arg)
{
	struct account *acc = arg;
	return account_auth(acc, username, password, realm);
}


static void notify_handler(struct sip *sip, const struct sip_msg *msg,
			   void *arg)
{
	struct dialog *dialog = arg;

	if (mbuf_get_left(msg->mb)) {
		info("----- Dialog NOTIFY to %r from %r-----\n%b",
		     &msg->to.auri, &msg->from.auri,
		     mbuf_buf(msg->mb), mbuf_get_left(msg->mb));
	}

	(void)sip_treply(NULL, sip, msg, 200, "OK");

	if (dialog->shutdown)
		mem_deref(dialog);
}


static void close_handler(int err, const struct sip_msg *msg,
			  const struct sipevent_substate *substate,
			  void *arg)
{
	struct dialog *dialog = arg;
	(void)substate;

	info("dialog: subscribe from %s to %s closed: %s (%u %r)\n",
	     account_aor(ua_account(dialog->ua)),
	     contact_uri(dialog->contact),
	     err ? strerror(err) : "",
	     err ? 0 : msg->scode,
	     err ? 0 : &msg->reason);

	mem_deref(dialog);
}


static struct dialog *dialog_find(const struct ua *ua,
				  const struct contact *contact)
{
	struct le *le;

	for (le = dialogl.head; le; le = le->next) {

		struct dialog *dialog = le->data;

		if (dialog->ua == ua && dialog->contact == contact)
			return dialog;
	}

	return NULL;
}


static int dialog_subscribe(struct dialog *dialog)
{
	const char *aor = account_aor(ua_account(dialog->ua));
	const char *c_uri = contact_uri(dialog->contact);
	const char *routev[1];
	int err;
	routev[0] = ua_outbound(dialog->ua);

	info("dialog: subscribe from %s to %s\n", aor, c_uri);

	err = sipevent_subscribe(&dialog->sub, uag_sipevent_sock(),
				 c_uri, NULL,
				 aor, "dialog", NULL,
				 600, ua_cuser(dialog->ua),
				 routev, routev[0] ? 1 : 0,
				 auth_handler, ua_account(dialog->ua),
				 true, NULL, notify_handler,
				 close_handler, dialog,
				 "Accept:"
				 " application/dialog-info\r\n");

	if (err) {
		warning("dialog: subscribe ERROR: %m\n", err);
		mem_deref(dialog);
	}

	return err;
}


static int dialog_subscribe_all(struct ua *ua)
{
	struct dialog *dialog;
	int err = 0;
	/* We will subscribe to all dialog contacts*/
	struct contacts *contacts = baresip_contacts();
	struct le *le;

	for (le = list_head(contact_list(contacts)); le; le = le->next) {

		struct contact *c = le->data;
		struct sip_addr *addr = contact_addr(c);
		struct pl val;

		if (0 == msg_param_decode(&addr->params, "dialog", &val) &&
		    0 == pl_strcasecmp(&val, "p2p")) {

			if (dialog_find(ua, c))
				continue;

			dialog = mem_zalloc(sizeof(*dialog), destructor);
			if (!dialog) {
				err = ENOMEM;
				return err;
			}

			list_append(&dialogl, &dialog->le, dialog);
			dialog->ua = mem_ref(ua);
			dialog->contact = mem_ref(c);
			tmr_init(&dialog->tmr);
			tmr_start(&dialog->tmr, 1000, tmr_handler, dialog);
		}
	}

	return err;
}


static int dialog_unsubscribe(struct dialog *dialog)
{
	info("dialog: unsubscribe from %s to %s\n",
	     account_aor(ua_account(dialog->ua)),
	     contact_uri(dialog->contact));

	dialog->shutdown = true;

	if (dialog->sub) {
		dialog->sub = mem_deref(dialog->sub);
		tmr_start(&dialog->tmr, 500, deref_handler, dialog);
	}
	else
		mem_deref(dialog);

	return 0;
}


static int dialog_unsubscribe_all(struct ua *ua)
{
	/* We will unsubscribe from all dialog contacts*/
	struct contacts *contacts = baresip_contacts();
	struct le *le;

	for (le = list_head(contact_list(contacts)); le; le = le->next) {

		struct contact *c = le->data;
		struct sip_addr *addr = contact_addr(c);
		struct pl val;

		if (0 == msg_param_decode(&addr->params, "dialog", &val) &&
		    0 == pl_strcasecmp(&val, "p2p")) {

			struct dialog *dialog = dialog_find(ua, c);
			if (dialog)
				dialog_unsubscribe(dialog);
		}
	}

	return 0;
}


static void ua_event_handler(struct ua *ua, enum ua_event ev,
			     struct call *call, const char *prm, void *arg)
{
	const struct account *acc = ua_account(ua);
	(void)call;
	(void)prm;
	(void)arg;

	if (ev == UA_EVENT_REGISTER_OK) {

		if (account_dialog(acc))
			dialog_subscribe_all(ua);
	}
	else if (ev == UA_EVENT_SHUTDOWN ||
		 (ev == UA_EVENT_UNREGISTERING &&
		  str_cmp(account_sipnat(acc), "outbound") == 0)) {
		if (account_dialog(acc))
			dialog_unsubscribe_all(ua);
	}
}


static void tmr_handler(void *arg)
{
	struct dialog *dialog = arg;
	if (dialog_subscribe(dialog))
		tmr_start(&dialog->tmr, 1000, tmr_handler, dialog);
}


static int module_init(void)
{
	list_init(&dialogl);

	return uag_event_register(ua_event_handler, NULL);
}


static int module_close(void)
{
	uag_event_unregister(ua_event_handler);
	list_flush(&dialogl);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(dialog) = {
	"dialog",
	"application",
	module_init,
	module_close,
};
