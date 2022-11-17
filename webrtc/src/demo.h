/**
 * @file demo.h  Baresip WebRTC demo
 *
 * Copyright (C) 2010 Alfred E. Heggestad
 */


/*
 * NOTE: API under development
 */


struct session {
	struct le le;
	struct peer_connection *pc;
	struct rtc_configuration pc_config;
	struct http_conn *conn_pending;
	char id[4];
};

int session_new(struct list *sessl, struct session **sessp);
int session_start(struct session *sess,
		  const struct rtc_configuration *pc_config,
		  const struct mnat *mnat, const struct menc *menc);
struct session *session_lookup(const struct list *sessl,
			       const struct http_msg *msg);
int  session_handle_ice_candidate(struct session *sess,
				  const struct odict *od);
void session_close(struct session *sess, int err);


/*
 * Demo
 */

int  demo_init(const char *server_cert, const char *www_path,
	       const char *ice_server,
	       const char *stun_user, const char *stun_pass);
void demo_close(void);
