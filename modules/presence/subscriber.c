/**
 * @file subscriber.c Presence subscriber
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <re.h>
#include <baresip.h>
#include "presence.h"


/*
 * Subscriber - we subscribe to the status information of N resources.
 *
 * For each entry in the address book marked with ;presence=p2p,
 * we send a SUBSCRIBE to that person, and expect to receive
 * a NOTIFY when her status changes.
 */


/** Constants */
enum {
	SHUTDOWN_DELAY = 500  /**< Delay before un-registering [ms] */
};


struct presence {
	struct le le;
	struct sipsub *sub;
	struct tmr tmr;
	enum presence_status status;
	unsigned failc;
	struct contact *contact;
	struct ua *ua;
	bool shutdown;
};

static struct list presencel;


static void tmr_handler(void *arg);


static uint32_t wait_term(const struct sipevent_substate *substate)
{
	uint32_t wait;

	switch (substate->reason) {

	case SIPEVENT_DEACTIVATED:
	case SIPEVENT_TIMEOUT:
		wait = 5;
		break;

	case SIPEVENT_REJECTED:
	case SIPEVENT_NORESOURCE:
		wait = 3600;
		break;

	case SIPEVENT_PROBATION:
	case SIPEVENT_GIVEUP:
	default:
		wait = 300;
		if (pl_isset(&substate->retry_after))
			wait = max(wait, pl_u32(&substate->retry_after));
		break;
	}

	return wait;
}


static uint32_t wait_fail(unsigned failc)
{
	switch (failc) {

	case 1:  return 30;
	case 2:  return 300;
	case 3:  return 3600;
	default: return 86400;
	}
}


static void notify_handler(struct sip *sip, const struct sip_msg *msg,
			   void *arg)
{
	enum presence_status status = PRESENCE_CLOSED;
	struct presence *pres = arg;
	const struct sip_hdr *type_hdr, *length_hdr;
	struct pl pl;

	if (pres->shutdown)
		goto done;

	pres->failc = 0;

	type_hdr = sip_msg_hdr(msg, SIP_HDR_CONTENT_TYPE);

	if (!type_hdr) {

		length_hdr = sip_msg_hdr(msg, SIP_HDR_CONTENT_LENGTH);
		if (0 == pl_strcmp(&length_hdr->val, "0")) {

			status = PRESENCE_UNKNOWN;
			goto done;
		}
	}

	if (!type_hdr ||
	    0 != pl_strcasecmp(&type_hdr->val, "application/pidf+xml")) {

		if (type_hdr)
			warning("presence: unsupported content-type: '%r'\n",
				&type_hdr->val);

		sip_treplyf(NULL, NULL, sip, msg, false,
			    415, "Unsupported Media Type",
			    "Accept: application/pidf+xml\r\n"
			    "Content-Length: 0\r\n"
			    "\r\n");
		return;
	}

	if (!re_regex((const char *)mbuf_buf(msg->mb), mbuf_get_left(msg->mb),
		      "<basic[ \t]*>[^<]+</basic[ \t]*>", NULL, &pl, NULL)) {
	    if (!pl_strcasecmp(&pl, "open"))
		status = PRESENCE_OPEN;
	}

	if (!re_regex((const char *)mbuf_buf(msg->mb), mbuf_get_left(msg->mb),
		      "<rpid:away[ \t]*/>", NULL)) {

		status = PRESENCE_CLOSED;
	}
	else if (!re_regex((const char *)mbuf_buf(msg->mb),
			   mbuf_get_left(msg->mb),
			   "<rpid:busy[ \t]*/>", NULL)) {

		status = PRESENCE_BUSY;
	}
	else if (!re_regex((const char *)mbuf_buf(msg->mb),
			   mbuf_get_left(msg->mb),
			   "<rpid:on-the-phone[ \t]*/>", NULL)) {

		status = PRESENCE_BUSY;
	}

done:
	(void)sip_treply(NULL, sip, msg, 200, "OK");

	contact_set_presence(pres->contact, status);

	if (pres->shutdown)
		mem_deref(pres);
}


static void close_handler(int err, const struct sip_msg *msg,
			  const struct sipevent_substate *substate, void *arg)
{
	struct presence *pres = arg;
	uint32_t wait;

	pres->sub = mem_deref(pres->sub);

	info("presence: subscriber closed <%r>: ",
	     &contact_addr(pres->contact)->auri);

	if (substate) {
		info("%s", sipevent_reason_name(substate->reason));
		wait = wait_term(substate);
	}
	else if (msg) {
		info("%u %r", msg->scode, &msg->reason);
		wait = wait_fail(++pres->failc);
	}
	else {
		info("%m", err);
		wait = wait_fail(++pres->failc);
	}

	info("; will retry in %u secs (failc=%u)\n", wait, pres->failc);

	tmr_start(&pres->tmr, wait * 1000, tmr_handler, pres);

	contact_set_presence(pres->contact, PRESENCE_UNKNOWN);
}


static void destructor(void *arg)
{
	struct presence *pres = arg;

	debug("presence: subscriber destroyed\n");

	list_unlink(&pres->le);
	tmr_cancel(&pres->tmr);
	mem_deref(pres->contact);
	mem_deref(pres->sub);
	mem_deref(pres->ua);
}


static void deref_handler(void *arg)
{
	struct presence *pres = arg;
	mem_deref(pres);
}


static int auth_handler(char **username, char **password,
			const char *realm, void *arg)
{
	return account_auth(arg, username, password, realm);
}


static int subscribe(struct presence *pres)
{
	const char *routev[1];
	struct ua *ua;
	char uri[256];
	int err;

	/* We use the first UA */
	ua = uag_find_aor(NULL);
	if (!ua) {
		warning("presence: no UA found\n");
		return ENOENT;
	}

	mem_deref(pres->ua);
	pres->ua = mem_ref(ua);

	pl_strcpy(&contact_addr(pres->contact)->auri, uri, sizeof(uri));

	routev[0] = ua_outbound(ua);

	err = sipevent_subscribe(&pres->sub, uag_sipevent_sock(), uri, NULL,
				 ua_aor(ua), "presence", NULL, 600,
				 ua_cuser(ua), routev, routev[0] ? 1 : 0,
				 auth_handler, ua_prm(ua), true, NULL,
				 notify_handler, close_handler, pres,
				 "%H", ua_print_supported, ua);
	if (err) {
		warning("presence: sipevent_subscribe failed: %m\n", err);
	}

	return err;
}


static void tmr_handler(void *arg)
{
	struct presence *pres = arg;

	if (subscribe(pres)) {
		tmr_start(&pres->tmr, wait_fail(++pres->failc) * 1000,
			  tmr_handler, pres);
	}
}


static int presence_alloc(struct contact *contact)
{
	struct presence *pres;

	pres = mem_zalloc(sizeof(*pres), destructor);
	if (!pres)
		return ENOMEM;

	pres->status  = PRESENCE_UNKNOWN;
	pres->contact = mem_ref(contact);

	tmr_init(&pres->tmr);
	tmr_start(&pres->tmr, 1000, tmr_handler, pres);

	list_append(&presencel, &pres->le, pres);

	return 0;
}


int subscriber_init(void)
{
	struct contacts *contacts = baresip_contacts();
	struct le *le;
	int err = 0;

	for (le = list_head(contact_list(contacts)); le; le = le->next) {

		struct contact *c = le->data;
		struct sip_addr *addr = contact_addr(c);
		struct pl val;

		if (0 == msg_param_decode(&addr->params, "presence", &val) &&
		    0 == pl_strcasecmp(&val, "p2p")) {

			err |= presence_alloc(le->data);
		}
	}

	info("Subscribing to %u contacts\n", list_count(&presencel));

	return err;
}


void subscriber_close(void)
{
	list_flush(&presencel);
}


void subscriber_close_all(void)
{
	struct le *le;

	info("presence: subscriber: closing %u subs\n",
	     list_count(&presencel));

	le = presencel.head;
	while (le) {

		struct presence *pres = le->data;
		le = le->next;

		debug("presence: shutdown: sub=%p\n", pres->sub);

		pres->shutdown = true;
		if (pres->sub) {
			pres->sub = mem_deref(pres->sub);
			tmr_start(&pres->tmr, SHUTDOWN_DELAY,
				  deref_handler, pres);
		}
		else
			mem_deref(pres);
	}
}
