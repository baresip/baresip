/**
 * @file http.c  HTTP functions
 *
 * Copyright (C) 2020 - 2022 Alfred E. Heggestad
 */

#include <string.h>
#include <re.h>
#include <baresip.h>


const char *http_extension_to_mimetype(const char *ext)
{
	if (0 == str_casecmp(ext, "html")) return "text/html";
	if (0 == str_casecmp(ext, "js"))   return "text/javascript";

	return "application/octet-stream";  /* default */
}


int http_reply_json(struct http_conn *conn, const char *sessid,
		    const struct odict *od)
{
	const char *ctype = "application/json";
	char *buf = NULL;
	int err;

	if (!conn)
		return EINVAL;

	err = re_sdprintf(&buf, "%H", json_encode_odict, od);
	if (err)
		goto out;

	err = http_reply(conn, 201, "Created",
			 "Content-Type: %s\r\n"
			 "Content-Length: %zu\r\n"
			 "Access-Control-Allow-Origin: *\r\n"
			 "Session-ID: %s\r\n"
			 "\r\n"
			 "%s",
			 ctype, str_len(buf), sessid, buf);

 out:
	mem_deref(buf);

	return err;
}
