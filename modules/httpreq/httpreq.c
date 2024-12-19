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
 *   - basic, digest and bearer authentication
 *   - TLS
 *
 * Commands:
 * http_setauth     - Sets user and password. If no parameter is specified then
 *                    user and password is cleared.
 * http_setbearer   - Sets bearer token. If no parameter is specified then the
 *                    bearer is cleared.
 * http_setbody     - Sets HTTP body (for POST, PUT requests). If no parameter
 *                    is specified then the body is cleared.
 * http_settimeout  - Sets timeout (currently) only for DNS requests.
 * http_setctype    - Sets content type for HTTP header. If no parameter is
 *                    specified then the content type is cleared.
 * http_setcert     - Sets client certificate file.
 * http_addheader   - Adds a custom header (without newline).
 * http_clrheaders  - Clears all custom headers.
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
	struct network *net;
	struct http_cli *client;
	struct http_reqconn *conn;
};


static void destructor(void *arg)
{
	struct httpreq_data *d = arg;
	mem_deref(d->client);
	mem_deref(d->conn);
	mem_deref(d->net);
}


static struct httpreq_data *d = NULL;


static void http_resph(int err, const struct http_msg *msg, void *arg)
{
	struct pl pl;
	const struct pl *v;
	const struct http_hdr *hdr;
	(void) arg;

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
	if (!d->net)
		err = net_alloc(&d->net, d->cfg);

	if (err) {
		warning("httpreq: could not create network\n");
		return err;
	}

	if (!d->client)
		err = http_client_alloc(&d->client, net_dnsc(d->net));

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
	struct pl *pl;
	int err;

	if (!plp)
		return EINVAL;

	pl = *plp;

	err = ensure_alloc();
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


static int send_request(void *arg, const struct pl *met)
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

	err = send_request(arg, &pl);
	if (err)
		re_hprintf(pf, "Usage:\nhttp_get <uri>\n");

	return err;
}


static int cmd_httppost(struct re_printf *pf, void *arg)
{
	int err = 0;
	struct pl pl = PL("POST");

	err = send_request(arg, &pl);
	if (err)
		re_hprintf(pf, "Usage:\nhttp_post <uri>\n");

	return err;
}


static int cmd_setauth(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	struct pl user = PL_INIT;
	struct pl pass = PL_INIT;
	int err;
	(void)pf;

	err = ensure_alloc();
	if (err)
		return err;

	if (carg->prm) {
		err = re_regex(carg->prm, strlen(carg->prm), "[^ ]* [^ ]*",
				&user, &pass);
		if (err)
			err = re_regex(carg->prm, strlen(carg->prm), "[^ ]*",
					&user);
	}
	else {
		re_hprintf(pf, "Usage:\nhttp_setauth <user> [pass]\n");
		return err;
	}

	if (err)
		return err;

	return http_reqconn_set_auth(d->conn,
			pl_isset(&user) ? &user : NULL,
			pl_isset(&pass) ? &pass : NULL);
}


static int cmd_setbearer(struct re_printf *pf, void *arg)
{
	struct pl pl;
	struct pl *plp = &pl;
	int err;
	(void)pf;

	err = pl_opt_arg(&plp, arg);
	if (err)
		return err;

	return http_reqconn_set_bearer(d->conn, plp);
}


static int cmd_setbody(struct re_printf *pf, void *arg)
{
	struct pl pl;
	struct pl *plp = &pl;
	struct mbuf *mb;
	int err;
	(void)pf;

	err = pl_opt_arg(&plp, arg);
	if (err || !plp)
		return err;

	mb = mbuf_alloc(plp->l);
	if (!mb)
		return ENOMEM;

	err = mbuf_write_pl(mb, plp);
	if (err)
		goto out;

	err = http_reqconn_set_body(d->conn, mb);
out:
	mem_deref(mb);
	return err;
}


static int cmd_setctype(struct re_printf *pf, void *arg)
{
	struct pl pl;
	struct pl *plp = &pl;
	int err;
	(void)pf;

	err = pl_opt_arg(&plp, arg);
	if (err)
		return err;

	return http_reqconn_set_ctype(d->conn, plp);
}


static int cmd_addheader(struct re_printf *pf, void *arg)
{
	struct pl pl = PL_INIT;
	int err = pl_set_arg(&pl, arg);
	if (err) {
		re_hprintf(pf, "Usage:\nhttp_addheader <header>\n");
		return err;
	}

	return http_reqconn_add_header(d->conn, &pl);
}


static int cmd_clrheader(struct re_printf *pf, void *arg)
{
	(void)pf;
	(void)arg;

	(void) http_reqconn_clr_header(d->conn);
	return 0;
}


static int cmd_clear(struct re_printf *pf, void *arg)
{
	(void)pf;
	(void)arg;

	d->conn = mem_deref(d->conn);
	d->client = mem_deref(d->client);
	return 0;
}


#ifdef USE_TLS
static int cmd_setcert(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	int err = ensure_carg_alloc(carg);
	if (err) {
		re_hprintf(pf, "Usage:\nhttp_setcert <certfile>\n");
		return err;
	}

	return http_client_set_cert(d->client, carg->prm);
}


static int cmd_setkey(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	int err = ensure_carg_alloc(carg);
	if (err) {
		re_hprintf(pf, "Usage:\nhttp_setkey <keyfile>\n");
		return err;
	}

	return http_client_set_key(d->client, carg->prm);
}


static int cmd_sethostname(struct re_printf *pf, void *arg)
{
	struct pl pl;
	struct pl *plp = &pl;
	int err;
	(void)pf;

	err = pl_opt_arg(&plp, arg);
	if (err)
		return err;

	return http_client_set_tls_hostname(d->client, plp);
}


static int ca_handler(const struct pl *pl, void *arg)
{
	struct mbuf *mb;
	char *parm;
	int err;
	(void) arg;

	if (!pl_isset(pl))
		return EINVAL;

	err = ensure_alloc();
	if (err)
		return err;

	mb = mbuf_alloc(pl->l + 1);
	mbuf_write_pl(mb, pl);
	mbuf_write_u8(mb, 0);
	mbuf_set_pos(mb, 0);

	parm = (char*) mbuf_buf(mb);
	err = http_client_add_ca(d->client, parm);

	mem_deref(mb);

	/* ignore err, just print warning */
	if (err)
		warning("httpreq: could not add ca %s\n", parm);

	return 0;
}
#endif


static int cmd_settimeout(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	struct http_conf conf;
	uint32_t v;
	int err;

	err = ensure_carg_alloc(carg);
	if (err) {
		re_hprintf(pf, "Usage:\nhttp_settimeout <ms>\n");
		return err;
	}

	v = (uint32_t) atoi(carg->prm);

	conf.conn_timeout = v;
	conf.recv_timeout = 60000;
	conf.idle_timeout = 900000;

	return http_client_set_config(d->client, &conf);
}


static const struct cmd cmdv[] = {

{"http_get",  0, CMD_PRM, "httpreq: send HTTP GET request",  cmd_httpget  },
{"http_post", 0, CMD_PRM, "httpreq: send HTTP POST request", cmd_httppost },
{"http_setauth", 0, CMD_PRM, "httpreq: set user and password", cmd_setauth },
{"http_setbearer", 0, CMD_PRM, "httpreq: set bearer token", cmd_setbearer },
{"http_setbody", 0, CMD_PRM, "httpreq: set body", cmd_setbody },
{"http_settimeout", 0, CMD_PRM, "httpreq: set timeout in ms", cmd_settimeout },
{"http_setctype", 0, CMD_PRM, "httpreq: set content-type", cmd_setctype },
{"http_addheader", 0, CMD_PRM, "httpreq: add a custom header "
	"(without newline)", cmd_addheader },
{"http_clrheaders", 0, CMD_PRM, "httpreq: clear custom headers",
	cmd_clrheader },
{"http_clear", 0, CMD_PRM, "httpreq: clear all internal data", cmd_clear },
#ifdef USE_TLS
{"http_setcert", 0, CMD_PRM, "httpreq: set client certificate file",
	cmd_setcert },
{"http_setkey", 0, CMD_PRM, "httpreq: set client private key file",
	cmd_setkey },
{"http_sethostname", 0, CMD_PRM, "httpreq: set hostname for the hostname "
	"check", cmd_sethostname },
#endif

};


static int module_init(void)
{
	int err = 0;
#ifdef USE_TLS
	struct pl pl;
	char *buf;
#endif

	info("httpreq: module init\n");
	d = mem_zalloc(sizeof(*d), destructor);
	if (!d)
		return ENOMEM;

	d->cfg = &conf_config()->net;

#ifdef USE_TLS
	if (!conf_get(conf_cur(), "httpreq_hostname", &pl)) {
		err = ensure_alloc();
		if (err)
			return err;

		err = http_reqconn_set_tls_hostname(d->conn, &pl);
	}

	if (!conf_get(conf_cur(), "httpreq_cert", &pl)) {
		err |= ensure_alloc();
		if (err)
			return err;

		err = pl_strdup(&buf, &pl);
		if (err)
			return err;

		err = http_client_set_cert(d->client, buf);
		mem_deref(buf);
	}

	if (!conf_get(conf_cur(), "httpreq_key", &pl)) {
		err |= ensure_alloc();
		if (err)
			return err;

		err = pl_strdup(&buf, &pl);
		if (err)
			return err;

		err = http_client_set_key(d->client, buf);
		mem_deref(buf);
	}

	err |= conf_apply(conf_cur(), "httpreq_ca", ca_handler, d->client);
	if (err)
		return err;
#endif

	err = cmd_register(baresip_commands(), cmdv, RE_ARRAY_SIZE(cmdv));
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
