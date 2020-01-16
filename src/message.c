/**
 * @file src/message.c  SIP MESSAGE -- RFC 3428
 *
 * Copyright (C) 2010 Creytiv.com
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
			   const struct sip_msg *msg)
{
	static const char ctype_text[] = "text/plain";
	struct pl ctype_pl = {ctype_text, sizeof(ctype_text)-1};
	(void)ua;

	if (msg_ctype_cmp(&msg->ctyp, "text", "plain") && lsnr->recvh) {

		lsnr->recvh(ua, &msg->from.auri, &ctype_pl,
			    msg->mb, lsnr->arg);

		(void)sip_reply(uag_sip(), msg, 200, "OK");
	}
	else {
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

	ua = uag_find(&msg->uri.user);
	if (!ua) {
		(void)sip_treply(NULL, uag_sip(), msg, 404, "Not Found");
		return true;
	}

	while (le) {
		struct message_lsnr *lsnr = le->data;

		le = le->next;

		handle_message(lsnr, ua, msg);

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

	err = pl_strdup(&uri, &addr.auri);
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
