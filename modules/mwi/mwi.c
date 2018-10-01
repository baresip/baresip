/**
 * @file mwi.c Message Waiting Indication (RFC 3842)
 *
 * Copyright (C) 2010 - 2015 Creytiv.com
 */
#include <string.h>
#include <re.h>
#include <baresip.h>


/**
 * @defgroup mwi mwi
 *
 * Message Waiting Indication
 *
 */


struct mwi {
	struct le le;
	struct sipsub *sub;
	struct ua *ua;
	struct tmr tmr;
	bool shutdown;
};

static struct tmr tmr;
static struct list mwil;


static void destructor(void *arg)
{
	struct mwi *mwi = arg;

	tmr_cancel(&mwi->tmr);
	list_unlink(&mwi->le);
	mem_deref(mwi->sub);
	mem_deref(mwi->ua);
}


static void deref_handler(void *arg)
{
	struct mwi *mwi = arg;
	mem_deref(mwi);
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
	struct mwi *mwi = arg;

	if (mbuf_get_left(msg->mb)) {
		ua_event(mwi->ua, UA_EVENT_MWI_NOTIFY, NULL, "%b",
			  mbuf_buf(msg->mb), mbuf_get_left(msg->mb));
	}

	(void)sip_treply(NULL, sip, msg, 200, "OK");

	if (mwi->shutdown)
		mem_deref(mwi);
}


static void close_handler(int err, const struct sip_msg *msg,
			  const struct sipevent_substate *substate,
			  void *arg)
{
	struct mwi *mwi = arg;
	(void)substate;

	info("mwi: subscription for %s closed: %s (%u %r)\n",
	     ua_aor(mwi->ua),
	     err ? strerror(err) : "",
	     err ? 0 : msg->scode,
	     err ? 0 : &msg->reason);

	mem_deref(mwi);
}


static int mwi_subscribe(struct ua *ua)
{
	const char *routev[1];
	struct mwi *mwi;
	int err;

	mwi = mem_zalloc(sizeof(*mwi), destructor);
	if (!mwi)
		return ENOMEM;

	list_append(&mwil, &mwi->le, mwi);
	mwi->ua = mem_ref(ua);

	routev[0] = ua_outbound(ua);

	info("mwi: subscribing to messages for %s\n", ua_aor(ua));

	err = sipevent_subscribe(&mwi->sub, uag_sipevent_sock(), ua_aor(ua),
				 NULL, ua_aor(ua), "message-summary", NULL,
	                         600, ua_cuser(ua),
				 routev, routev[0] ? 1 : 0,
	                         auth_handler, ua_account(ua), true, NULL,
				 notify_handler, close_handler, mwi,
				 "Accept:"
				 " application/simple-message-summary\r\n");
	if (err) {
		warning("mwi: subscribe ERROR: %m\n", err);
	}

	if (err)
		mem_deref(mwi);

	return err;
}


static struct mwi *mwi_find(const struct ua *ua)
{
	struct le *le;

	for (le = mwil.head; le; le = le->next) {

		struct mwi *mwi = le->data;

		if (mwi->ua == ua)
			return mwi;
	}

	return NULL;
}


static void ua_event_handler(struct ua *ua,
			     enum ua_event ev,
			     struct call *call,
			     const char *prm,
			     void *arg )
{
	(void)call;
	(void)prm;
	(void)arg;

	if (ev == UA_EVENT_REGISTER_OK) {

		if (!mwi_find(ua) &&
		    (str_casecmp(account_mwi(ua_account(ua)), "yes") == 0))
			mwi_subscribe(ua);
	}
	else if (ev == UA_EVENT_SHUTDOWN) {

		struct mwi *mwi = mwi_find(ua);

		if (mwi) {

			info("mwi: shutdown of %s\n", ua_aor(ua));
			mwi->shutdown = true;

			if (mwi->sub) {
				mwi->sub = mem_deref(mwi->sub);
				tmr_start(&mwi->tmr, 500, deref_handler, mwi);
			}
			else
				mem_deref(mwi);
		}
	}
}


static void tmr_handler(void *arg)
{
	struct le *le;

	(void)arg;

	for (le = list_head(&mwil); le; le = le->next) {
		struct mwi *mwi = le->data;
		mwi_subscribe(mwi->ua);
	}
}


static int module_init(void)
{
	list_init(&mwil);
	tmr_start(&tmr, 1, tmr_handler, 0);

	return uag_event_register(ua_event_handler, NULL);
}


static int module_close(void)
{
	uag_event_unregister(ua_event_handler);
	tmr_cancel(&tmr);
	list_flush(&mwil);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(mwi) = {
	"mwi",
	"application",
	module_init,
	module_close,
};
