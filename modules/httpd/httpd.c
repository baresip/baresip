/**
 * @file httpd.c Webserver UI module
 *
 * Copyright (C) 2010 - 2015 Creytiv.com
 */
#include <re.h>
#include <baresip.h>


/**
 * @defgroup httpd httpd
 *
 * HTTP Server module for the User-Interface
 *
 *
 * Open your favourite web browser and point it to http://127.0.0.1:8000/
 * Example URLs:
 *
 \verbatim
  http://127.0.0.1:8000?h                  -- Print the Help menu
  http://127.0.0.1:8000?d1234@target.com   -- Make an outgoing call
 \endverbatim
 *
 * The following options can be configured:
 *
 \verbatim
  http_listen     0.0.0.0:8000         # IP-address and port to listen on
 \endverbatim
 */

enum {HTTP_PORT = 8000};

static struct http_sock *httpsock;


static int handle_input(struct re_printf *pf, const struct pl *pl)
{
	if (!pl)
		return 0;

	if (pl->l > 1 && pl->p[0] == '/')
		return ui_input_long_command(pf, pl);
	else
		return ui_input_pl(pf, pl);
}


static int html_print_head(struct re_printf *pf, void *unused)
{
	(void)unused;

	return re_hprintf(pf,
			  "<html>\n"
			  "<head>\n"
			  "<title>Baresip v" BARESIP_VERSION "</title>\n"
			  "</head>\n");
}


static int html_print_cmd(struct re_printf *pf, const struct pl *prm)
{
	struct pl params;

	if (!pf || !prm)
		return EINVAL;

	if (pl_isset(prm)) {
		params.p = prm->p + 1;
		params.l = prm->l - 1;
	}
	else {
		params.p = "h";
		params.l = 1;
	}

	return re_hprintf(pf,
			  "%H"
			  "<body>\n"
			  "<pre>\n"
			  "%H"
			  "</pre>\n"
			  "</body>\n"
			  "</html>\n",
			  html_print_head, NULL,
			  handle_input, &params);
}


static int html_print_raw(struct re_printf *pf, const struct pl *prm)
{
	struct pl params;

	if (!pf || !prm)
		return EINVAL;

	if (pl_isset(prm)) {
		params.p = prm->p + 1;
		params.l = prm->l - 1;
	}
	else {
		params.p = "h";
		params.l = 1;
	}

	return re_hprintf(pf,
			  "%H",
			  handle_input, &params);
}


static void http_req_handler(struct http_conn *conn,
			     const struct http_msg *msg, void *arg)
{
	struct mbuf *mb;
	int err;
	char *buf = NULL;
	struct pl nprm;
	(void)arg;

	mb = mbuf_alloc(8192);
	if (!mb)
		return;

	err = re_sdprintf(&buf, "%H", uri_header_unescape, &msg->prm);
	if (err)
		goto error;

	pl_set_str(&nprm, buf);

	if (0 == pl_strcasecmp(&msg->path, "/")) {

		err = mbuf_printf(mb, "%H", html_print_cmd, &nprm);
		if (!err) {
			http_reply(conn, 200, "OK",
				 "Content-Type: text/html;charset=UTF-8\r\n"
				 "Content-Length: %zu\r\n"
				 "Access-Control-Allow-Origin: *\r\n"
				 "\r\n"
				 "%b",
				 mb->end,
				 mb->buf, mb->end);
		}

	}
	else if (0 == pl_strcasecmp(&msg->path, "/raw/")) {

		err = mbuf_printf(mb, "%H", html_print_raw, &nprm);
		if (!err) {
			http_reply(conn, 200, "OK",
				 "Content-Type: text/plain;charset=UTF-8\r\n"
				 "Content-Length: %zu\r\n"
				 "Access-Control-Allow-Origin: *\r\n"
				 "\r\n"
				 "%b",
				 mb->end,
				 mb->buf, mb->end);
		}

	}
	else {
		goto error;
	}
	mem_deref(mb);
	mem_deref(buf);

	return;

 error:
	mem_deref(mb);
	mem_deref(buf);
	http_ereply(conn, 404, "Not Found");
}


static int output_handler(const char *str)
{
	(void)str;

	return 0;
}


static struct ui ui_http = {
	.name = "http",
	.outputh = output_handler
};


static int module_init(void)
{
	struct sa laddr;
	int err;

	if (conf_get_sa(conf_cur(), "http_listen", &laddr)) {
		sa_set_str(&laddr, "0.0.0.0", HTTP_PORT);
	}

	err = http_listen(&httpsock, &laddr, http_req_handler, NULL);
	if (err)
		return err;

	ui_register(baresip_uis(), &ui_http);

	info("httpd: listening on %J\n", &laddr);

	return 0;
}


static int module_close(void)
{
	ui_unregister(&ui_http);

	httpsock = mem_deref(httpsock);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(httpd) = {
	"httpd",
	"application",
	module_init,
	module_close,
};
