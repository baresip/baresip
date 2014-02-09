/**
 * @file dtls_srtp.h DTLS-SRTP Internal api
 *
 * Copyright (C) 2010 Creytiv.com
 */


enum {
	LAYER_SRTP = 20,
	LAYER_DTLS = 20, /* must be above zero */
};

struct sock {
	const struct dtls_srtp *ds;
	struct dtls_flow *dtls;
	struct srtp_stream *tx;
	struct srtp_stream *rx;
	struct udp_helper *uh_srtp;
	void *app_sock;
	bool negotiated;
	bool is_rtp;
};

struct key {
	uint8_t key[256];
	size_t  key_len;
	uint8_t salt[256];
	size_t  salt_len;
};


/* dtls.c */
int dtls_alloc_selfsigned(struct tls **tlsp, const char *aor,
			  const char *srtp_profile);
int dtls_print_sha1_fingerprint(struct re_printf *pf, const struct tls *tls);
int dtls_print_sha256_fingerprint(struct re_printf *pf, const struct tls *tls);


/* srtp.c */
int  srtp_stream_add(struct srtp_stream **sp, const char *profile,
		     const struct key *key, bool tx);
int  srtp_install(struct sock *sock);


/* tls_udp.c */
struct dtls_flow;

typedef void (dtls_estab_h)(int err, struct dtls_flow *tc,
			    const char *profile,
			    const struct key *client_key,
			    const struct key *server_key,
			    void *arg);

int dtls_flow_alloc(struct dtls_flow **flowp, struct tls *tls,
		    struct udp_sock *us, dtls_estab_h *estabh, void *arg);
int dtls_flow_start(struct dtls_flow *flow, const struct sa *peer,
		    bool active);
int dtls_get_remote_fingerprint(const struct dtls_flow *flow, const char *type,
				struct tls_fingerprint *fp);
