/**
 * @file httpd.c Webserver UI module
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <stdlib.h>
#include <string.h>
#include <re.h>
#include <baresip.h>

struct ui_st {
	struct ui *ui; /* base class */
	struct http_sock *httpsock;
	void *arg;
};

#define BARESIP_HTTP_HEAD "<html>\n<head>\n<title>Baresip v"BARESIP_VERSION"</title>\n</head>\n"
#define BARESIP_HTTP_ERR BARESIP_HTTP_HEAD"<body><pre>\nHTTP snapshot error\n</pre>\n</body>\n"

int httpd_parse_command(struct http_conn *conn, const struct http_msg *msg);
int html_print_head(struct re_printf *pf, void *unused);

/* We only allow one instance */
static struct ui_st *_ui;
static struct ui *httpd;

int html_print_head(struct re_printf *pf, void *unused)
{
	(void)unused;

	return re_hprintf(pf, BARESIP_HTTP_HEAD);
}

static int html_print_help(struct re_printf *pf, const struct http_msg *req)
{
	struct pl params;

	if (!pf || !req)
		return EINVAL;

	params.p = "h";
	params.l = 1;

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

static int html_input_str(struct re_printf *pf, char *str)
{
	struct pl params;

	if (!pf || !str)
		return EINVAL;

	params.p = str;
	params.l = strlen(str);

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

int httpd_parse_command(struct http_conn *conn, const struct http_msg *msg)
{
	int err = 0;

	if (pl_isset(&msg->path)) {
		char *com = 0;
		pl_strdup(&com, &msg->path);
		// is here command?
		if (com) {
			// simple command with http reply
			http_creply(conn, 200, "OK",
				"text/html;charset=UTF-8",
				"%H", html_input_str, &com[1]);
			info("httpd: %s\n", &com[1]);
			// command with parameters?
			if (pl_isset(&msg->prm)) {
				char *prm = 0;
				pl_strdup(&prm, &msg->prm);
				http_creply(conn, 200, "OK",
					"text/html;charset=UTF-8",
					"%H", html_input_str, prm);
				mem_deref(prm);
			}
		}
		mem_deref(com);
		return err;
	}
	// error http reply
	http_ereply(conn, 404, "Not Found");
	return err;
}


static void http_req_handler(struct http_conn *conn,
			     const struct http_msg *msg, void *arg)
{
	(void)arg;

	if (!pl_isset(&msg->path)) {
		http_ereply(conn, 404, "Not Found");
		return;
	}

	// default action - help message
	if (0 == pl_strcasecmp(&msg->path, "/"))
		http_creply(conn, 200, "OK",
			"text/html;charset=UTF-8",
			"%H", html_print_help, msg);
	else
		httpd_parse_command(conn, msg);
}

static int server_setup(struct ui_st *st)
{
	struct sa laddr;
	int err;

	if (conf_get_sa(conf_cur(), "http_listen", &laddr)) {
		sa_set_str(&laddr, "0.0.0.0", 8000);
	}

	err = http_listen(&st->httpsock, &laddr, http_req_handler, st);
	if (err)
		return err;

	info("httpd: listening on %J\n", &laddr);
	return 0;
}

static void ui_destructor(void *arg)
{
	struct ui_st *st = arg;
	st->httpsock = mem_deref(st->httpsock);
	mem_deref(st->ui);
	_ui = NULL;
}

static int ui_alloc(struct ui_st **stp)
{
	struct ui_st *st;
	int err;

	if (!stp)
		return EINVAL;

	if (_ui) {
		*stp = mem_ref(_ui);
		return 0;
	}

	st = mem_zalloc(sizeof(*st), ui_destructor);
	if (!st)
		return ENOMEM;

	st->ui = mem_ref(httpd);

	err = server_setup(st);
	if (err) {
		info("httpd: could not setup server: %m\n", err);
		err = 0;
	}

	if (err)
		mem_deref(st);
	else
		*stp = _ui = st;

	return err;
}

static int output_handler(const char *str)
{
    (void)str;
    return 0;
}

static struct ui ui_httpd = {
	.name = "httpd",
	.outputh = output_handler
};


static int module_init(void)
{
	int err;

	err = ui_alloc(&_ui);
	if (err)
		return err;

	ui_register(&ui_httpd);

	return
		err;
}


static int module_close(void)
{
	httpd = mem_deref(httpd);
	return 0;
}

EXPORT_SYM const struct mod_export DECL_EXPORTS(httpd) = {
	"httpd",
	"ui",
	module_init,
	module_close,
};
