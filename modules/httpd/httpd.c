/**
 * @file httpd.c Webserver UI module
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <re.h>
#include <baresip.h>


static struct http_sock *httpsock;


static int html_print_head(struct re_printf *pf, void *unused)
{
	(void)unused;

	return re_hprintf(pf,
			  "<html>\n"
			  "<head>\n"
			  "<title>Baresip v" BARESIP_VERSION "</title>\n"
			  "</head>\n");
}


static int html_print_cmd(struct re_printf *pf, const struct http_msg *req)
{
	struct pl params;

	if (!pf || !req)
		return EINVAL;

	if (pl_isset(&req->prm)) {
		params.p = req->prm.p + 1;
		params.l = req->prm.l - 1;
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
			  ui_input_pl, &params);
}


static void http_req_handler(struct http_conn *conn,
			     const struct http_msg *msg, void *arg)
{
	(void)arg;

	if (0 == pl_strcasecmp(&msg->path, "/")) {

		http_creply(conn, 200, "OK",
			    "text/html;charset=UTF-8",
			    "%H", html_print_cmd, msg);
	}
	else {
		http_ereply(conn, 404, "Not Found");
	}
}


static int output_handler(const char *str)
{
	(void)str;

	/* TODO: print 'str' to all active HTTP connections */

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
		sa_set_str(&laddr, "0.0.0.0", 8000);
	}

	err = http_listen(&httpsock, &laddr, http_req_handler, NULL);
	if (err)
		return err;

	ui_register(&ui_http);

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
