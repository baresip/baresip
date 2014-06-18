/**
 * @file dtls_srtp.h DTLS-SRTP Internal api
 *
 * Copyright (C) 2010 Creytiv.com
 */


enum {
	LAYER_SRTP = 20,
	LAYER_DTLS = 20, /* must be above zero */
};

struct comp {
	const struct dtls_srtp *ds; /* parent */
	struct dtls_sock *dtls_sock;
	struct tls_conn *tls_conn;
	struct srtp_stream *tx;
	struct srtp_stream *rx;
	struct udp_helper *uh_srtp;
	void *app_sock;
	bool negotiated;
	bool is_rtp;
};

/* dtls.c */
int dtls_print_sha1_fingerprint(struct re_printf *pf, const struct tls *tls);
int dtls_print_sha256_fingerprint(struct re_printf *pf, const struct tls *tls);


/* srtp.c */
int  srtp_stream_add(struct srtp_stream **sp, enum srtp_suite suite,
		     const uint8_t *key, size_t key_size, bool tx);
int  srtp_install(struct comp *comp);
