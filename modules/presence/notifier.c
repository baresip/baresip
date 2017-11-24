/**
 * @file notifier.c Presence notifier
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <re.h>
#include <baresip.h>
#include "presence.h"


/*
 * Notifier - other people are subscribing to the status of our AOR.
 * we must maintain a list of active notifications. we receive a SUBSCRIBE
 * message from peer, and send NOTIFY to all peers when the Status changes
 */


struct notifier {
	struct le le;
	struct sipnot *not;
	struct ua *ua;
};

static struct list notifierl;


static const char *presence_status_str(enum presence_status st)
{
	switch (st) {

	case PRESENCE_OPEN:   return "open";
	case PRESENCE_CLOSED: return "closed";
	default: return "?";
	}
}


static int notify(struct notifier *not, enum presence_status status)
{
	const char *aor = ua_aor(not->ua);
	struct mbuf *mb;
	int err;

	mb = mbuf_alloc(1024);
	if (!mb)
		return ENOMEM;

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
		    ,aor, presence_status_str(status), aor);
	if (err)
		goto out;

	mb->pos = 0;

	err = sipevent_notify(not->not, mb, SIPEVENT_ACTIVE, 0, 0);
	if (err) {
		warning("presence: notify to %s failed (%m)\n", aor, err);
	}

 out:
	mem_deref(mb);
	return err;
}


static void sipnot_close_handler(int err, const struct sip_msg *msg,
				 void *arg)
{
	struct notifier *not = arg;

	if (err) {
		info("presence: notifier closed (%m)\n", err);
	}
	else if (msg) {
		info("presence: notifier closed (%u %r)\n",
		     msg->scode, &msg->reason);
	}

	mem_deref(not);
}


static void destructor(void *arg)
{
	struct notifier *not = arg;

	list_unlink(&not->le);
	mem_deref(not->not);
	mem_deref(not->ua);
}


static int auth_handler(char **username, char **password,
			const char *realm, void *arg)
{
	return account_auth(arg, username, password, realm);
}


static int notifier_alloc(struct notifier **notp,
			  const struct sip_msg *msg,
			  const struct sipevent_event *se, struct ua *ua)
{
	struct notifier *not;
	int err;

	if (!msg || !se)
		return EINVAL;

	not = mem_zalloc(sizeof(*not), destructor);
	if (!not)
		return ENOMEM;

	not->ua   = mem_ref(ua);

	err = sipevent_accept(&not->not, uag_sipevent_sock(),
			      msg, NULL, se, 200, "OK",
			      600, 600, 600,
			      ua_cuser(not->ua), "application/pidf+xml",
			      auth_handler, ua_account(not->ua), true,
			      sipnot_close_handler, not, NULL);
	if (err) {
		warning("presence: sipevent_accept failed: %m\n", err);
		goto out;
	}

	list_append(&notifierl, &not->le, not);

 out:
	if (err)
		mem_deref(not);
	else if (notp)
		*notp = not;

	return err;
}


static int notifier_add(const struct sip_msg *msg, struct ua *ua)
{
	const struct sip_hdr *hdr;
	struct sipevent_event se;
	struct notifier *not;
	int err;

	hdr = sip_msg_hdr(msg, SIP_HDR_EVENT);
	if (!hdr)
		return EPROTO;

	err = sipevent_event_decode(&se, &hdr->val);
	if (err)
		return err;

	if (pl_strcasecmp(&se.event, "presence")) {
		info("presence: unexpected event '%r'\n", &se.event);
		return EPROTO;
	}

	err = notifier_alloc(&not, msg, &se, ua);
	if (err)
		return err;

	(void)notify(not, ua_presence_status(ua));

	return 0;
}


void notifier_update_status(struct ua *ua)
{
	struct le *le;

	for (le = notifierl.head; le; le = le->next) {

		struct notifier *not = le->data;

		if (not->ua == ua)
			(void)notify(not, ua_presence_status(not->ua));
	}
}


static bool sub_handler(const struct sip_msg *msg, void *arg)
{
	struct ua *ua = arg;

	if (notifier_add(msg, ua))
		(void)sip_treply(NULL, uag_sip(), msg, 400, "Bad Presence");

	return true;
}


int notifier_init(void)
{
	uag_set_sub_handler(sub_handler);

	return 0;
}


void notifier_close(void)
{
	list_flush(&notifierl);
	uag_set_sub_handler(NULL);
}
