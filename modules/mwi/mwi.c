/**
 * @file mwi.c Message Waiting Indication (RFC 3842)
 *
 * Copyright (C) 2010 - 2015 Alfred E. Heggestad
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
		bevent_ua_emit(BEVENT_MWI_NOTIFY, mwi->ua, "%b",
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
	     account_aor(ua_account(mwi->ua)),
	     err ? strerror(err) : "",
	     err ? 0 : msg->scode,
	     err ? 0 : &msg->reason);

	mem_deref(mwi);
}


static int mwi_subscribe(struct ua *ua)
{
	const char *aor = account_aor(ua_account(ua));
	const char *routev[1];
	struct mwi *mwi;
	int err;

	mwi = mem_zalloc(sizeof(*mwi), destructor);
	if (!mwi)
		return ENOMEM;

	list_append(&mwil, &mwi->le, mwi);
	mwi->ua = mem_ref(ua);

	routev[0] = ua_outbound(ua);

	info("mwi: subscribing to messages for %s\n", aor);

	err = sipevent_subscribe(&mwi->sub, uag_sipevent_sock(), aor,
				 NULL, aor, "message-summary", NULL,
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


static void event_handler(enum bevent_ev ev, struct bevent *event, void *arg)
{
	struct ua *ua = bevent_get_ua(event);
	const struct account *acc = ua_account(ua);
	(void)arg;

	if (ev == BEVENT_REGISTER_OK) {

		if (!mwi_find(ua) && account_mwi(acc))
			mwi_subscribe(ua);
	}
	else if (ev == BEVENT_SHUTDOWN ||
		 (ev == BEVENT_UNREGISTERING &&
		  str_cmp(account_sipnat(acc), "outbound") == 0)) {

		struct mwi *mwi = mwi_find(ua);

		if (mwi) {

			info("mwi: shutdown of %s\n", account_aor(acc));

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

	return bevent_register(event_handler, NULL);
}


static int module_close(void)
{
	bevent_unregister(event_handler);
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
