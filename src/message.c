/**
 * @file message.c  SIP MESSAGE -- RFC 3428
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <re.h>
#include <baresip.h>
#include "core.h"


static struct sip_lsnr *lsnr;
static message_recv_h *recvh;
static void *recvarg;


static void handle_message(struct ua *ua, const struct sip_msg *msg)
{
	static const char *ctype_text = "text/plain";
	struct pl mtype;
	(void)ua;

	if (re_regex(msg->ctype.p, msg->ctype.l, "[^;]+", &mtype))
		mtype = msg->ctype;

	if (0==pl_strcasecmp(&mtype, ctype_text) && recvh) {
		recvh(&msg->from.auri, &msg->ctype, msg->mb, recvarg);
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
	struct ua *ua;

	(void)arg;

	if (pl_strcmp(&msg->met, "MESSAGE"))
		return false;

	ua = uag_find(&msg->uri.user);
	if (!ua) {
		(void)sip_treply(NULL, uag_sip(), msg, 404, "Not Found");
		return true;
	}

	handle_message(ua, msg);

	return true;
}


static void resp_handler(int err, const struct sip_msg *msg, void *arg)
{
	struct ua *ua = arg;

	(void)ua;

	if (err) {
		(void)re_fprintf(stderr, " \x1b[31m%m\x1b[;m\n", err);
		return;
	}

	if (msg->scode >= 300) {
		(void)re_fprintf(stderr, " \x1b[31m%u %r\x1b[;m\n",
				 msg->scode, &msg->reason);
	}
}


int message_init(message_recv_h *h, void *arg)
{
	int err;

	err = sip_listen(&lsnr, uag_sip(), true, request_handler, NULL);
	if (err)
		return err;

	recvh   = h;
	recvarg = arg;

	return 0;
}


void message_close(void)
{
	lsnr = mem_deref(lsnr);
}


/**
 * Send SIP instant MESSAGE to a peer
 *
 * @param ua    User-Agent object
 * @param peer  Peer SIP Address
 * @param msg   Message to send
 *
 * @return 0 if success, otherwise errorcode
 */
int message_send(struct ua *ua, const char *peer, const char *msg)
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

	err = sip_req_send(ua, "MESSAGE", uri, resp_handler, ua,
			   "Accept: text/plain\r\n"
			   "Content-Type: text/plain\r\n"
			   "Content-Length: %zu\r\n"
			   "\r\n%s",
			   str_len(msg), msg);

	mem_deref(uri);

	return err;
}
