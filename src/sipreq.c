/**
 * @file sipreq.c  SIP Authenticated Request
 *
 * Copyright (C) 2011 Creytiv.com
 */
#include <re.h>
#include <baresip.h>
#include "core.h"


/** SIP Authenticated Request */
struct sip_req {
	struct sip_loopstate ls;
	struct sip_dialog *dlg;
	struct sip_auth *auth;
	struct sip_request *req;
	char *method;
	char *fmt;
	sip_resp_h *resph;
	void *arg;
};


static int request(struct sip_req *sr);


static void destructor(void *arg)
{
	struct sip_req *sr = arg;

	mem_deref(sr->req);
	mem_deref(sr->auth);
	mem_deref(sr->dlg);
	mem_deref(sr->method);
	mem_deref(sr->fmt);
}


static void resp_handler(int err, const struct sip_msg *msg, void *arg)
{
	struct sip_req *sr = arg;

	if (err || sip_request_loops(&sr->ls, msg->scode))
		goto out;

	if (msg->scode < 200) {
		return;
	}
	else if (msg->scode < 300) {
		;
	}
	else {
		switch (msg->scode) {

		case 401:
		case 407:
			err = sip_auth_authenticate(sr->auth, msg);
			if (err) {
				err = (err == EAUTH) ? 0 : err;
				break;
			}

			err = request(sr);
			if (err)
				break;

			return;

		case 403:
			sip_auth_reset(sr->auth);
			break;
		}
	}

 out:
	if (sr->resph)
		sr->resph(err, msg, sr->arg);

	/* destroy now */
	mem_deref(sr);
}


static int auth_handler(char **username, char **password,
			const char *realm, void *arg)
{
	struct account *acc = arg;

	return account_auth(acc, username, password, realm);
}


static int request(struct sip_req *sr)
{
	return sip_drequestf(&sr->req, uag_sip(), true, sr->method, sr->dlg,
			     0, sr->auth, NULL, resp_handler,
			     sr, sr->fmt ? "%s" : NULL, sr->fmt);
}


int sip_req_send(struct ua *ua, const char *method, const char *uri,
		 sip_resp_h *resph, void *arg, const char *fmt, ...)
{
	const char *routev[1];
	struct sip_req *sr;
	int err;

	if (!ua || !method || !uri || !fmt)
		return EINVAL;

	routev[0] = ua_outbound(ua);

	sr = mem_zalloc(sizeof(*sr), destructor);
	if (!sr)
		return ENOMEM;

	sr->resph = resph;
	sr->arg   = arg;

	err = str_dup(&sr->method, method);

	if (fmt) {
		va_list ap;

		va_start(ap, fmt);
		err |= re_vsdprintf(&sr->fmt, fmt, ap);
		va_end(ap);
	}

	if (err)
		goto out;

	err = sip_dialog_alloc(&sr->dlg, uri, uri, NULL, ua_aor(ua),
			       routev[0] ? routev : NULL,
			       routev[0] ? 1 : 0);
	if (err)
		goto out;

	err = sip_auth_alloc(&sr->auth, auth_handler, ua_account(ua), true);
	if (err)
		goto out;

	err = request(sr);

 out:
	if (err)
		mem_deref(sr);

	return err;
}
