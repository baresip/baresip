/**
 * @file publisher.c Presence Publisher (RFC 3903)
 *
 * Copyright (C) 2010 Creytiv.com
 * Copyright (C) 2014 Juha Heinanen
 */

#include <string.h>
#include <re.h>
#include <baresip.h>
#include "presence.h"


struct publisher {
	struct le le;
	struct tmr tmr;
	unsigned failc;
	char *etag;
	unsigned int expires;
	unsigned int refresh;
	struct ua *ua;
};

static struct list publ = LIST_INIT;

static void tmr_handler(void *arg);
static int publish(struct publisher *pub);


static void response_handler(int err, const struct sip_msg *msg, void *arg)
{
	struct publisher *pub = arg;
	const struct sip_hdr *etag_hdr;

	if (err)
		return;

	if (msg->scode < 200) {
		return;
	}

	if (msg->scode < 300) {

		if (pub->expires == 0)
			return;

		etag_hdr = sip_msg_xhdr(msg, "SIP-ETag");
		if (etag_hdr) {
			mem_deref(pub->etag);
			pl_strdup(&(pub->etag), &(etag_hdr->val));
			pub->refresh = 1;
			tmr_start(&pub->tmr, pub->expires * 900,
				  tmr_handler, pub);
		}
		else {
			warning("%s: publisher got 200 OK without etag\n",
				ua_aor(pub->ua));
		}
	}
	else if (msg->scode == 412) {

		mem_deref(pub->etag);
		pub->etag = NULL;
		pub->refresh = 0;
		publish(pub);

	}
	else {
		warning("%s: publisher got error response %u %r\n",
			ua_aor(pub->ua), msg->scode, &msg->reason);
	}

	return;
}


/* move this to presence.c */
static const char *presence_status_str(enum presence_status st)
{
	switch (st) {

	case PRESENCE_OPEN:   return "open";
	case PRESENCE_CLOSED: return "closed";
	case PRESENCE_UNKNOWN: return "unknown";
	default: return "?";
	}
}


static int print_etag_header(struct re_printf *pf, const char *etag)
{
	if (!etag)
		return 0;

	return re_hprintf(pf, "SIP-If-Match: %s\r\n", etag);
}


static int publish(struct publisher *pub)
{
	int err;
	const char *aor = ua_aor(pub->ua);
	struct mbuf *mb;

	mb = mbuf_alloc(1024);
	if (!mb)
		return ENOMEM;

	if (pub->expires && !pub->refresh)
		err = mbuf_printf(mb,
	"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?>\r\n"
	"<presence xmlns=\"urn:ietf:params:xml:ns:pidf\"\r\n"
	"    xmlns:dm=\"urn:ietf:params:xml:ns:pidf:data-model\"\r\n"
	"    xmlns:rpid=\"urn:ietf:params:xml:ns:pidf:rpid\"\r\n"
	"    entity=\"%s\">\r\n"
	"  <dm:person id=\"p4159\"><rpid:activities/></dm:person>\r\n"
	"  <tuple id=\"t4109\">\r\n"
	"    <status>\r\n"
	"      <basic>%s</basic>\r\n"
	"    </status>\r\n"
	"    <contact>%s</contact>\r\n"
	"  </tuple>\r\n"
	"</presence>\r\n"
		  ,aor,
		  presence_status_str(ua_presence_status(pub->ua)), aor);
	else
		err = mbuf_printf(mb, "");
	if (err)
		goto out;

	mb->pos = 0;

	err = sip_req_send(pub->ua, "PUBLISH", aor,
			   pub->expires ? response_handler : NULL,
			   pub,
			   "%s"
			   "Event: presence\r\n"
			   "Expires: %u\r\n"
			   "%H"
			   "Content-Length: %zu\r\n"
			   "\r\n"
			   "%b",
			   pub->expires
			   ? "Content-Type: application/pidf+xml\r\n"
			   : "",

			   pub->expires,
			   print_etag_header, pub->etag,
			   mbuf_get_left(mb),
			   mbuf_buf(mb),
			   mbuf_get_left(mb));
	if (err) {
		warning("publisher: send PUBLISH: (%m)\n", err);
	}

out:
	mem_deref(mb);

	return err;
}


/* move to presence.c */
static uint32_t wait_fail(unsigned failc)
{
	switch (failc) {

	case 1:  return 30;
	case 2:  return 300;
	case 3:  return 3600;
	default: return 86400;
	}
}


static void tmr_handler(void *arg)
{
	struct publisher *pub = arg;

	if (publish(pub))
		tmr_start(&pub->tmr, wait_fail(++pub->failc) * 1000,
			  tmr_handler, pub);
	else
		pub->failc = 0;
}


static void destructor(void *arg)
{
	struct publisher *pub = arg;

	list_unlink(&pub->le);
	tmr_cancel(&pub->tmr);
	mem_deref(pub->ua);
	mem_deref(pub->etag);
}


void publisher_update_status(struct ua *ua)
{
	struct le *le;

	for (le = publ.head; le; le = le->next) {

		struct publisher *pub = le->data;

		if (pub->ua == ua) {
			pub->refresh = 0;
			publish(pub);
		}
	}
}


static int publisher_alloc(struct ua *ua)
{
	struct publisher *pub;

	pub = mem_zalloc(sizeof(*pub), destructor);
	if (!pub)
		return ENOMEM;

	pub->ua = mem_ref(ua);
	pub->expires = account_pubint(ua_account(ua));

	tmr_init(&pub->tmr);
	tmr_start(&pub->tmr, 10, tmr_handler, pub);

	list_append(&publ, &pub->le, pub);

	return 0;
}


static void pub_ua_event_handler(struct ua *ua,
				 enum ua_event ev,
				 struct call *call,
				 const char *prm,
				 void *arg )
{
	(void)call;
	(void)prm;
	(void)arg;

	if (account_pubint(ua_account(ua)) == 0)
		return;

	if (ev == UA_EVENT_REGISTER_OK) {
		if (ua_presence_status(ua) == PRESENCE_UNKNOWN) {
			ua_presence_status_set(ua, PRESENCE_OPEN);
			publisher_update_status(ua);
		}
	}
}


int publisher_init(void)
{
	struct le *le;
	int err = 0;

	uag_event_register(pub_ua_event_handler, NULL);

	for (le = list_head(uag_list()); le; le = le->next) {

		struct ua *ua = le->data;
		struct account *acc = ua_account(ua);

		if (account_pubint(acc) == 0)
			continue;

		err |= publisher_alloc(ua);
	}

	if (err)
		return err;

	return 0;
}


void publisher_close(void)
{
	struct le *le;

	uag_event_unregister(pub_ua_event_handler);

	for (le = list_head(&publ); le; le = le->next) {

		struct publisher *pub = le->data;

		ua_presence_status_set(pub->ua, PRESENCE_CLOSED);
		pub->expires = 0;
		publish(pub);
	}

	list_flush(&publ);
}
