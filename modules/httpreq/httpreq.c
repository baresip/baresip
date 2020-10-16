/**
 * @file httpreq.c HTTP request module
 *
 * Copyright (C) 2020 Commend.com - c.spielberger@commend.com
 */

#include <stdlib.h>
#include <string.h>
#include <re.h>
#include <baresip.h>


/**
 * @defgroup httpreq httpreq
 *
 * HTTP request client connection
 *
 * Combines libre structs http_cli and http_reqcon to provide HTTP requests.
 * Supports:
 *   - GET, POST requests
 *   - basic, digest authentication
 *
 * Commands:
 * http_setauth     - Sets user and password. If no parameter is specified then
 *                    user and password is cleared.
 * http_setbody     - Sets HTTP body (for POST, PUT requests). If no parameter
 *                    is specified then the body is cleared.
 * http_settimeout  - Sets timeout (currently) only for DNS requests.
 * http_setctype    - Sets content type for HTTP header. If no parameter is
 *                    specified then the content type is cleared.
 * http_clear       - Clears all internal data.
 * http_get         - Sends an HTTP GET request and performs authentication if
 *                    requested by the HTTP server and http_setauth was invoked
 *                    before.
 * http_post        - Sends an HTTP POST request and performs authentication if
 *                    requested by the HTTP server and http_setauth was invoked
 *                    before. Use at least http_setbody before this command.
 */

struct httpreq_data {
	const struct config_net *cfg;
	struct http_cli *client;
	struct http_reqconn *conn;
};


static void destructor(void *arg)
{
	struct httpreq_data *d = arg;
	mem_deref(d->client);
	mem_deref(d->conn);
}


static struct httpreq_data *d = NULL;


static void http_resph(int err, const struct http_msg *msg, void *arg)
{
	(void) arg;
	struct pl pl;
	const struct pl *v;
	const struct http_hdr *hdr;

	if (err) {
		warning("httpreq: HTTP response error (%m)\n", err);
	}
	else if (!msg) {
		warning("httpreq: HTTP empty response\n");
	}
	else {
		hdr = http_msg_hdr(msg, HTTP_HDR_CONTENT_TYPE);
		v = &hdr->val;
		info("httpreq: HTTP response:\n");
		re_fprintf(stdout, "%H\n", http_msg_print,  msg);
		if (msg->mb && !re_regex(v->p, v->l, "text/")) {
			pl_set_mbuf(&pl, msg->mb);
			re_fprintf(stdout, "\n%r\n", &pl);
		}
	}
}


static int ensure_alloc(void)
{
	int err = 0;
	struct network *net = baresip_network();

	if (!net) {
		warning("httpreq: no baresip network\n");
		return err;
	}

	if (!d->client)
		err = http_client_alloc(&d->client, net_dnsc(net));

	if (err) {
		warning("httpreq: could not alloc http client\n");
		return err;
	}

	if (!d->conn)
		err = http_reqconn_alloc(&d->conn, d->client, http_resph, NULL,
				NULL);

	if (err)
		warning("httpreq: could not alloc http request connection\n");

	return err;
}


static int ensure_carg_alloc(const struct cmd_arg *carg)
{
	if (!carg || !str_isset(carg->prm))
		return EINVAL;

	return ensure_alloc();
}


static int pl_set_arg(struct pl *pl, const struct cmd_arg *carg)
{
	int err = ensure_carg_alloc(carg);
	if (err)
		return err;

	pl->p = carg->prm;
	pl->l = strlen(carg->prm);
	return 0;
}


static int pl_opt_arg(struct pl **plp, const struct cmd_arg *carg)
{
	struct pl *pl = *plp;
	if (!plp)
		return EINVAL;

	int err = ensure_alloc();
	if (err)
		return err;

	if (!carg || !str_isset(carg->prm)) {
		*plp = NULL;
		return 0;
	}

	pl->p = carg->prm;
	pl->l = strlen(carg->prm);
	return 0;
}


static int send_request(struct re_printf *pf, void *arg, const struct pl *met)
{
	struct pl uri;
	int err = pl_set_arg(&uri, arg);
	if (err)
		return err;

	err = http_reqconn_set_method(d->conn, met);
	if (err)
		return err;

	err = http_reqconn_send(d->conn, &uri);
	return err;
}


static int cmd_httpget(struct re_printf *pf, void *arg)
{
	int err = 0;
	struct pl pl = PL("GET");

	err = send_request(pf, arg, &pl);
	if (err)
		re_hprintf(pf, "Usage:\nhttp_get <uri>\n");

	return err;
}


static int cmd_httppost(struct re_printf *pf, void *arg)
{
	int err = 0;
	struct pl pl = PL("POST");

	err = send_request(pf, arg, &pl);
	if (err)
		re_hprintf(pf, "Usage:\nhttp_post <uri>\n");

	return err;
}


static int cmd_setauth(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	struct pl user = PL_INIT;
	struct pl pass = PL_INIT;
	int err = ensure_alloc();
	if (err)
		return err;

	err = re_regex(carg->prm, strlen(carg->prm), "[^ ]* [^ ]*",
			&user, &pass);
	if (err)
		err = re_regex(carg->prm, strlen(carg->prm), "[^ ]*", &user);

	return http_reqconn_set_auth(d->conn,
			pl_isset(&user) ? &user : NULL,
			pl_isset(&pass) ? &pass : NULL);
}


static int cmd_setbody(struct re_printf *pf, void *arg)
{
	struct pl pl;
	struct pl *plp = &pl;
	int err = pl_opt_arg(&plp, arg);
	if (err)
		return err;

	return http_reqconn_set_body(d->conn, plp);
}


static int cmd_setctype(struct re_printf *pf, void *arg)
{
	struct pl pl;
	struct pl *plp = &pl;
	int err = pl_opt_arg(&plp, arg);
	if (err)
		return err;

	return http_reqconn_set_ctype(d->conn, plp);
}


static int cmd_clear(struct re_printf *pf, void *arg)
{
	(void) arg;
	d->conn = mem_deref(d->conn);
	d->client = mem_deref(d->client);
	return 0;
}


static int cmd_settimeout(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	uint32_t v;
	int err = ensure_carg_alloc(carg);
	if (err) {
		re_hprintf(pf, "Usage:\nhttp_settimeout <ms>\n");
		return err;
	}

	v = (uint32_t) atoi(carg->prm);
	return http_client_set_timeout(d->client, v);
}


static const struct cmd cmdv[] = {

{"http_get",  0, CMD_PRM, "httpreq: send HTTP GET request",  cmd_httpget  },
{"http_post", 0, CMD_PRM, "httpreq: send HTTP POST request", cmd_httppost },
{"http_setauth", 0, CMD_PRM, "httpreq: set user and password", cmd_setauth },
{"http_setbody", 0, CMD_PRM, "httpreq: set body", cmd_setbody },
{"http_settimeout", 0, CMD_PRM, "httpreq: set timeout in ms", cmd_settimeout },
{"http_setctype", 0, CMD_PRM, "httpreq: set content-type", cmd_setctype },
{"http_clear", 0, CMD_PRM, "httpreq: clear all internal data", cmd_clear },

};


static int module_init(void)
{
	int err = 0;

	info("httpreq: module init\n");
	d = mem_zalloc(sizeof(*d), destructor);
	if (!d)
		return ENOMEM;

	d->cfg = &conf_config()->net;
	err = cmd_register(baresip_commands(), cmdv, ARRAY_SIZE(cmdv));
	if (err) {
		d->client = mem_deref(d->client);
		d->conn = mem_deref(d->conn);
	}

	return err;
}


static int module_close(void)
{
	info("httpreq: module closed\n");

	cmd_unregister(baresip_commands(), cmdv);
	d = mem_deref(d);

	return 0;
}


const struct mod_export DECL_EXPORTS(httpreq) = {
	"httpreq",
	"application",
	module_init,
	module_close
};
