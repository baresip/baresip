/**
 * @file src/message.c  SIP MESSAGE -- RFC 3428
 *
 * Copyright (C) 2010 Alfred E. Heggestad
 */
#include <re.h>
#include <baresip.h>
#include "core.h"


struct message {
	struct list lsnrl;          /* struct message_lsnr */
	struct sip_lsnr *sip_lsnr;
};

struct message_lsnr {
	struct le le;
	message_recv_h *recvh;
	void *arg;
};


static void destructor(void *data)
{
	struct message *message = data;

	list_flush(&message->lsnrl);
	mem_deref(message->sip_lsnr);
}


static void listener_destructor(void *data)
{
	struct message_lsnr *lsnr = data;

	list_unlink(&lsnr->le);
}


static void handle_message(struct message_lsnr *lsnr, struct ua *ua,
			   const struct sip_msg *msg, bool hdld)
{
	static const char ctype_text[] = "text/plain";
	static const char ctype_app[] = "application/json";
	struct pl text_plain = {ctype_text, sizeof(ctype_text)-1};
	struct pl app_json = {ctype_app, sizeof(ctype_app)-1};
	struct pl *ctype_pl = NULL;
	struct account *acc = ua_account(ua);
	int err;

	if (account_uas_isset(acc)) {
		err = uas_req_auth(ua, msg);
		if (err)
			return;
	}

	if (msg_ctype_cmp(&msg->ctyp, "text", "plain")) {
		ctype_pl = &text_plain;
	}
	else if (msg_ctype_cmp(&msg->ctyp, "application", "json")) {
		ctype_pl = &app_json;
	}

	if (ctype_pl) {

		if (lsnr->recvh)
			lsnr->recvh(ua, &msg->from.auri, ctype_pl,
				    msg->mb, lsnr->arg);

		if (!hdld)
			(void)sip_reply(uag_sip(), msg, 200, "OK");
	}
	else if (!hdld) {
		(void)sip_replyf(uag_sip(), msg, 415, "Unsupported Media Type",
				 "Accept: %s\r\n"
				 "Content-Length: 0\r\n"
				 "\r\n",
				 ctype_text);
	}
}


static bool request_handler(const struct sip_msg *msg, void *arg)
{
	struct message *message = arg;
	struct ua *ua;
	struct le *le = message->lsnrl.head;
	bool hdld = false;

	if (pl_strcmp(&msg->met, "MESSAGE"))
		return false;

	ua = uag_find_msg(msg);
	if (!ua) {
		info("message: %r: UA not found: %H\n",
		     &msg->from.auri, uri_encode, &msg->uri);
		(void)sip_treply(NULL, uag_sip(), msg, 404, "Not Found");
		return true;
	}

	if (!ua_req_check_origin(ua, msg) || !ua_req_allowed(ua, msg)) {
		(void)sip_treply(NULL, uag_sip(), msg, 403, "Forbidden");
		return true;
	}
	while (le) {
		struct message_lsnr *lsnr = le->data;

		le = le->next;

		handle_message(lsnr, ua, msg, hdld);

		hdld = true;
	}

	return hdld;
}


/**
 * Create the messaging subsystem
 *
 * @param messagep Pointer to allocated messaging subsystem
 *
 * @return 0 if success, otherwise errorcode
 */
int message_init(struct message **messagep)
{
	struct message *message;

	if (!messagep)
		return EINVAL;

	message = mem_zalloc(sizeof(*message), destructor);
	if (!message)
		return ENOMEM;

	/* note: cannot create sip listener here, there is not UAs yet */

	*messagep = message;

	return 0;
}


/**
 * Listen to incoming SIP MESSAGE messages
 *
 * @param message Messaging subsystem
 * @param recvh   Message receive handler
 * @param arg     Handler argument
 *
 * @return 0 if success, otherwise errorcode
 */
int message_listen(struct message *message,
		   message_recv_h *recvh, void *arg)
{
	struct message_lsnr *lsnr;
	int err = 0;

	if (!message || !recvh)
		return EINVAL;

	/* create the SIP listener if it does not exist */
	if (!message->sip_lsnr) {

		err = sip_listen(&message->sip_lsnr, uag_sip(), true,
				 request_handler, message);
		if (err)
			goto out;
	}

	lsnr = mem_zalloc(sizeof(*lsnr), listener_destructor);
	if (!lsnr)
		return ENOMEM;

	lsnr->recvh = recvh;
	lsnr->arg = arg;

	list_append(&message->lsnrl, &lsnr->le, lsnr);

 out:
	return err;
}


/**
 * Stop listening to incoming SIP MESSAGE messages
 *
 * @param message Messaging subsystem
 * @param recvh   Message receive handler
 */
void message_unlisten(struct message *message, message_recv_h *recvh)
{
	struct le *le;

	if (!message)
		return;

	le = message->lsnrl.head;
	while (le) {
		struct message_lsnr *lsnr = le->data;
		le = le->next;

		if (lsnr->recvh == recvh)
			mem_deref(lsnr);
	}
}


/**
 * Send SIP instant MESSAGE to a peer
 *
 * @param ua    User-Agent object
 * @param peer  Peer SIP Address
 * @param msg   Message to send
 * @param resph Response handler
 * @param arg   Handler argument
 *
 * @return 0 if success, otherwise errorcode
 */
int message_send(struct ua *ua, const char *peer, const char *msg,
		 sip_resp_h *resph, void *arg)
{
	struct sip_addr addr;
	struct pl pl;
	char *uri = NULL;
	int err = 0;

	if (!ua || !peer || !msg)
		return EINVAL;

	pl_set_str(&pl, peer);

	err = sip_addr_decode(&addr, &pl);
	if (err)
		return err;

	if (pl_isset(&addr.params)) {
		err = re_sdprintf(&uri, "%r%r",
				  &addr.auri, &addr.params);
	}
	else {
		err = pl_strdup(&uri, &addr.auri);
	}
	if (err)
		return err;

	err = sip_req_send(ua, "MESSAGE", uri, resph, arg,
			   "Accept: text/plain\r\n"
			   "Content-Type: text/plain\r\n"
			   "Content-Length: %zu\r\n"
			   "\r\n%s",
			   str_len(msg), msg);

	mem_deref(uri);

	return err;
}


/**
 * Encode a SIP instant MESSAGE to a dictionary
 *
 * @param od    Dictionary to encode into
 * @param acc   User-Agent account
 * @param peer  Peer address URI
 * @param ctype Content type ("text/plain")
 * @param body  Buffer containing the SIP message body
 *
 * @return 0 if success, otherwise errorcode
 */
int message_encode_dict(struct odict *od, struct account *acc,
			const struct pl *peer, const struct pl *ctype,
			struct mbuf *body)
{
	int err = 0;
	char *buf1 = NULL;
	char *buf2 = NULL;
	char *buf3 = NULL;
	size_t pos = 0;

	if (!od || !acc || !pl_isset(peer))
		return EINVAL;

	err  = pl_strdup(&buf1, peer);
	err |= pl_strdup(&buf2, ctype);
	if (body) {
		pos = body->pos;
		err |= mbuf_strdup(body, &buf3, mbuf_get_left(body));
		body->pos = pos;
	}

	if (err)
		goto out;

	err |= odict_entry_add(od, "ua", ODICT_STRING, account_aor(acc));
	err |= odict_entry_add(od, "from",  ODICT_STRING, buf1);
	err |= odict_entry_add(od, "ctype", ODICT_STRING, buf2);
	if (buf3)
		err |= odict_entry_add(od, "body",  ODICT_STRING, buf3);

out:
	mem_deref(buf1);
	mem_deref(buf2);
	mem_deref(buf3);
	return err;
}
