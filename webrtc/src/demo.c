/**
 * @file demo.c  Baresip WebRTC demo
 *
 * Copyright (C) 2010 Alfred E. Heggestad
 */

#include <string.h>
#include <re.h>
#include <baresip.h>
#include "demo.h"


enum {
	HTTP_PORT  = 9000,
	HTTPS_PORT = 9001
};


static struct demo {
	struct list sessl;
	const struct mnat *mnat;
	const struct menc *menc;
	struct http_sock *httpsock;
	struct http_sock *httpssock;
	const char *www_path;
} demo;


static struct rtc_configuration pc_config = {
	.offerer = true
};


static int handle_put_sdp(struct session *sess, const struct http_msg *msg)
{
	struct session_description sd = {-1, NULL};
	int err = 0;

	info("demo: handle PUT sdp: content is '%r/%r'\n",
	     &msg->ctyp.type, &msg->ctyp.subtype);

	err = session_description_decode(&sd, msg->mb);
	if (err)
		return err;

	err = peerconnection_set_remote_descr(sess->pc, &sd);
	if (err) {
		warning("demo: set remote descr error"
			" (%m)\n", err);
		goto out;
	}

	if (sd.type == SDP_ANSWER) {

		err = peerconnection_start_ice(sess->pc);
		if (err) {
			warning("demo: failed to start ice"
				" (%m)\n", err);
			goto out;
		}
	}

out:
	session_description_reset(&sd);

	return err;
}


static void handle_get(struct http_conn *conn, const struct pl *path)
{
	const char *ext, *mime;
	struct mbuf *mb = NULL;
	char *buf = NULL;
	int err;

	err = re_sdprintf(&buf, "%s%r", demo.www_path, path);
	if (err)
		goto out;

	err = conf_loadfile(&mb, buf);
	if (err) {
		info("demo: not found: %s\n", buf);
		http_ereply(conn, 404, "Not Found");
		goto out;
	}

	ext = fs_file_extension(buf);
	mime = http_extension_to_mimetype(ext);

	info("demo: loaded file '%s', %zu bytes (%s)\n", buf, mb->end, mime);

	http_reply(conn, 200, "OK",
		   "Content-Type: %s;charset=UTF-8\r\n"
		   "Content-Length: %zu\r\n"
		   "Access-Control-Allow-Origin: *\r\n"
		   "\r\n"
		   "%b",
		   mime,
		   mb->end,
		   mb->buf, mb->end);

 out:
	mem_deref(mb);
	mem_deref(buf);
}


static void http_req_handler(struct http_conn *conn,
			     const struct http_msg *msg, void *arg)
{
	struct pl path = PL("/index.html");
	struct session *sess;
	struct odict *od = NULL;
	int err = 0;
	(void)arg;

	info("demo: request: met=%r, path=%r, prm=%r\n",
	     &msg->met, &msg->path, &msg->prm);

	if (msg->path.l > 1)
		path = msg->path;

	if (0 == pl_strcasecmp(&msg->met, "GET")) {

		handle_get(conn, &path);
	}
	else if (0 == pl_strcasecmp(&msg->met, "POST") &&
		 0 == pl_strcasecmp(&msg->path, "/connect/offerer")) {

		err = session_new(&demo.sessl, &sess);
		if (err)
			goto out;

		sess->pc_config = pc_config;
		sess->pc_config.offerer = false; /* browser is offerer */

		/* sync reply */
		http_reply(conn, 201, "Created",
			   "Content-Length: 0\r\n"
			   "Access-Control-Allow-Origin: *\r\n"
			   "Session-ID: %s\r\n"
			   "\r\n",
			   sess->id);
	}
	else if (0 == pl_strcasecmp(&msg->met, "POST") &&
		 0 == pl_strcasecmp(&msg->path, "/connect")) {

		err = session_new(&demo.sessl, &sess);
		if (err)
			goto out;

		sess->pc_config = pc_config;
		sess->pc_config.offerer = true; /* baresip-webrtc is offerer */

		err = session_start(sess, &sess->pc_config, demo.mnat,
				    demo.menc);
		if (err)
			goto out;

		/* async reply */
		mem_deref(sess->conn_pending);
		sess->conn_pending = mem_ref(conn);
	}
	else if (0 == pl_strcasecmp(&msg->met, "PUT") &&
		 0 == pl_strcasecmp(&msg->path, "/sdp")) {

		sess = session_lookup(&demo.sessl, msg);
		if (!sess) {
			http_ereply(conn, 404, "Session Not Found");
			return;
		}

		if (!sess->pc_config.offerer) {
			err = session_start(sess, &sess->pc_config, demo.mnat,
					    demo.menc);
			if (err)
				goto out;
		}

		if (msg->clen &&
		    msg_ctype_cmp(&msg->ctyp, "application", "json")) {
			err = handle_put_sdp(sess, msg);
			if (err)
				goto out;
		}

		if (sess->pc_config.offerer) {
			/* sync reply */
			http_reply(conn, 200, "OK",
				   "Content-Length: 0\r\n"
				   "Access-Control-Allow-Origin: *\r\n"
				   "\r\n");
		}
		else {
			/* async reply */
			mem_deref(sess->conn_pending);
			sess->conn_pending = mem_ref(conn);
		}
	}
	else if (0 == pl_strcasecmp(&msg->met, "PATCH")) {

		sess = session_lookup(&demo.sessl, msg);
		if (sess) {
			enum {HASH_SIZE = 4, MAX_DEPTH = 2};

			/* XXX: move json decode further up */
			err = json_decode_odict(&od, HASH_SIZE,
						(char *)mbuf_buf(msg->mb),
						mbuf_get_left(msg->mb),
						MAX_DEPTH);
			if (err) {
				warning("demo: candidate:"
					" could not decode json (%m)\n", err);
				goto out;
			}

			session_handle_ice_candidate(sess, od);

			/* sync reply */
			http_reply(conn, 204, "No Content",
				   "Content-Length: 0\r\n"
				   "Access-Control-Allow-Origin: *\r\n"
				   "\r\n");
		}
		else {
			http_ereply(conn, 404, "Session Not Found");
			return;
		}
	}
	else if (0 == pl_strcasecmp(&msg->met, "DELETE")) {

		/* draft-ietf-wish-whip-03 */
		info("demo: DELETE -> disconnect\n");

		sess = session_lookup(&demo.sessl, msg);
		if (sess) {
			info("demo: closing session %s\n", sess->id);
			session_close(sess, 0);

			http_reply(conn, 200, "OK",
				   "Content-Length: 0\r\n"
				   "Access-Control-Allow-Origin: *\r\n"
				   "\r\n");
		}
		else {
			http_ereply(conn, 404, "Session Not Found");
			return;
		}
	}
	else if (0 == pl_strcasecmp(&msg->met, "OPTIONS")) {
		http_reply(conn, 204, "OK",
				   "Content-Length: 0\r\n"
				   "Access-Control-Allow-Origin: *\r\n"
				   "Access-Control-Allow-Headers: *\r\n"
				   "\r\n");
	}
	else {
		warning("demo: not found: %r %r\n", &msg->met, &msg->path);
		http_ereply(conn, 404, "Not Found");
	}

 out:
	mem_deref(od);
	if (err)
		http_ereply(conn, 500, "Server Error");
}


int demo_init(const char *server_cert, const char *www_path,
	      const char *ice_server,
	      const char *stun_user, const char *credential)
{
	struct pl srv;
	struct sa laddr, laddrs;
	bool tls = true;
	int err;

	if (ice_server) {

		info("demo: using ICE server: %s\n", ice_server);

		pl_set_str(&srv, ice_server);

		err = stunuri_decode(&pc_config.ice_server, &srv);
		if (err) {
			warning("demo: invalid iceserver '%r' (%m)\n",
				&srv, err);
			return err;
		}
	}

	pc_config.stun_user = stun_user;
	pc_config.credential = credential;

	demo.mnat = mnat_find(baresip_mnatl(), "ice");
	if (!demo.mnat) {
		warning("demo: medianat 'ice' not found\n");
		return ENOENT;
	}

	demo.menc = menc_find(baresip_mencl(), "dtls_srtp");
	if (!demo.menc) {
		warning("demo: mediaenc 'dtls-srtp' not found\n");
		return ENOENT;
	}

	sa_set_str(&laddr, "0.0.0.0", HTTP_PORT);
	sa_set_str(&laddrs, "0.0.0.0", HTTPS_PORT);

	err = http_listen(&demo.httpsock, &laddr, http_req_handler, NULL);
	if (err)
		return err;

	err = https_listen(&demo.httpssock, &laddrs, server_cert,
			   http_req_handler, NULL);
	if (err)
		tls = false;

	demo.www_path = www_path;

	info("demo: listening on:\n");
	info("    http://127.0.0.1:%u/\n", sa_port(&laddr));

	if (!tls)
		return 0;

	info("    https://%j:%u/\n",
		net_laddr_af(baresip_network(), AF_INET), sa_port(&laddrs));

	return 0;
}


void demo_close(void)
{
	list_flush(&demo.sessl);

	demo.httpssock = mem_deref(demo.httpssock);
	demo.httpsock = mem_deref(demo.httpsock);
	pc_config.ice_server = mem_deref(pc_config.ice_server);
}
