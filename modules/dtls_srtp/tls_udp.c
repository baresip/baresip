/**
 * @file dtls_srtp/tls_udp.c  DTLS socket for DTLS-SRTP
 *
 * Copyright (C) 2010 Creytiv.com
 */

#define OPENSSL_NO_KRB5 1
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <re.h>
#include "dtls_srtp.h"


#define DEBUG_MODULE "tls_udp"
#define DEBUG_LEVEL 5
#include <re_dbg.h>


/* note: shadow struct in dtls.c */
struct tls {
	SSL_CTX *ctx;
};

struct dtls_flow {
	struct udp_helper *uh;
	struct udp_sock *us;
	struct tls *tls;
	struct tmr tmr;
	struct sa peer;
	SSL *ssl;
	BIO *sbio_out;
	BIO *sbio_in;
	bool up;
	dtls_estab_h *estabh;
	void *arg;
};


static void check_timer(struct dtls_flow *flow);


static int bio_create(BIO *b)
{
	b->init  = 1;
	b->num   = 0;
	b->ptr   = NULL;
	b->flags = 0;

	return 1;
}


static int bio_destroy(BIO *b)
{
	if (!b)
		return 0;

	b->ptr   = NULL;
	b->init  = 0;
	b->flags = 0;

	return 1;
}


static int bio_write(BIO *b, const char *buf, int len)
{
	struct dtls_flow *tc = b->ptr;
	struct mbuf *mb;
	enum {SPACE = 4}; /* sizeof TURN channel header */
	int err;

	mb = mbuf_alloc(SPACE + len);
	if (!mb)
		return -1;

	(void)mbuf_fill(mb, 0x00, SPACE);
	(void)mbuf_write_mem(mb, (void *)buf, len);

	mb->pos = SPACE;

	err = udp_send_helper(tc->us, &tc->peer, mb, tc->uh);
	if (err) {
		DEBUG_WARNING("udp_send_helper: %m\n", err);
	}

	mem_deref(mb);

	return err ? -1 : len;
}


static long bio_ctrl(BIO *b, int cmd, long num, void *ptr)
{
	(void)b;
	(void)num;
	(void)ptr;

	if (cmd == BIO_CTRL_FLUSH) {
		/* The OpenSSL library needs this */
		return 1;
	}

	return 0;
}


static struct bio_method_st bio_udp_send = {
	BIO_TYPE_SOURCE_SINK,
	"udp_send",
	bio_write,
	0,
	0,
	0,
	bio_ctrl,
	bio_create,
	bio_destroy,
	0
};


static int verify_callback(int ok, X509_STORE_CTX *ctx)
{
	(void)ok;
	(void)ctx;
	return 1;    /* We trust the certificate from peer */
}


#if defined (DTLS_CTRL_HANDLE_TIMEOUT) && defined(DTLS_CTRL_GET_TIMEOUT)
static void timeout(void *arg)
{
	struct dtls_flow *tc = arg;

	DTLSv1_handle_timeout(tc->ssl);

	check_timer(tc);
}
#endif


static void check_timer(struct dtls_flow *tc)
{
#if defined (DTLS_CTRL_HANDLE_TIMEOUT) && defined (DTLS_CTRL_GET_TIMEOUT)
	struct timeval tv = {0, 0};
	long x;

	x = DTLSv1_get_timeout(tc->ssl, &tv);

	if (x) {
		uint64_t delay = tv.tv_sec * 1000 + tv.tv_usec / 1000;

		tmr_start(&tc->tmr, delay, timeout, tc);
	}

#else
	(void)tc;
#endif
}


static int get_srtp_key_info(const struct dtls_flow *tc, char *name, size_t sz,
			     struct key *client_key, struct key *server_key)
{
	SRTP_PROTECTION_PROFILE *sel;
	const char *keymatexportlabel = "EXTRACTOR-dtls_srtp";
	uint8_t exportedkeymat[1024], *p;
	int keymatexportlen;
	size_t kl = 128, sl = 112;

	sel = SSL_get_selected_srtp_profile(tc->ssl);
	if (!sel)
		return ENOENT;

	str_ncpy(name, sel->name, sz);

	kl /= 8;
	sl /= 8;

	keymatexportlen = (int)(kl + sl)*2;
	if (keymatexportlen != 60) {
		DEBUG_WARNING("expected 60 bits, but keying material is %d\n",
			      keymatexportlen);
		return EINVAL;
	}

	if (!SSL_export_keying_material(tc->ssl, exportedkeymat,
					keymatexportlen,
					keymatexportlabel,
					strlen(keymatexportlabel),
					NULL, 0, 0)) {
		return ENOENT;
	}

	p = exportedkeymat;

	memcpy(client_key->key,  p, kl); p += kl;
	memcpy(server_key->key,  p, kl); p += kl;
	memcpy(client_key->salt, p, sl); p += sl;
	memcpy(server_key->salt, p, sl); p += sl;

	client_key->key_len  = server_key->key_len  = kl;
	client_key->salt_len = server_key->salt_len = sl;

	return 0;
}


static void destructor(void *arg)
{
	struct dtls_flow *flow = arg;

	if (flow->ssl) {
		(void)SSL_shutdown(flow->ssl);
		SSL_free(flow->ssl);
	}

	mem_deref(flow->uh);
	mem_deref(flow->us);

	tmr_cancel(&flow->tmr);
}


static bool recv_handler(struct sa *src, struct mbuf *mb, void *arg)
{
	struct dtls_flow *flow = arg;
	uint8_t b;
	int r;

	if (mbuf_get_left(mb) < 1)
		return false;

	/* ignore non-DTLS packets */
	b = mb->buf[mb->pos];
	if (b < 20 || b > 63)
		return false;

	if (!sa_cmp(src, &flow->peer, SA_ALL))
		return false;

	/* feed SSL data to the BIO */
	r = BIO_write(flow->sbio_in, mbuf_buf(mb), (int)mbuf_get_left(mb));
	if (r <= 0)
		return true;

	SSL_read(flow->ssl, mbuf_buf(mb), (int)mbuf_get_space(mb));

	if (!flow->up && SSL_state(flow->ssl) == SSL_ST_OK) {

		struct key client_key, server_key;
		char profile[256];
		int err;

		flow->up = true;

		err = get_srtp_key_info(flow, profile, sizeof(profile),
					&client_key, &server_key);
		if (err) {
			DEBUG_WARNING("SRTP key info: %m\n", err);
			return true;
		}

		flow->estabh(0, flow, profile,
			     &client_key, &server_key, flow->arg);
	}

	return true;
}


int dtls_flow_alloc(struct dtls_flow **flowp, struct tls *tls,
		    struct udp_sock *us, dtls_estab_h *estabh, void *arg)
{
	struct dtls_flow *flow;
	int err = ENOMEM;

	if (!flowp || !tls || !us || !estabh)
		return EINVAL;

	flow = mem_zalloc(sizeof(*flow), destructor);
	if (!flow)
		return ENOMEM;

	flow->tls    = tls;
	flow->us     = mem_ref(us);
	flow->estabh = estabh;
	flow->arg    = arg;

	err = udp_register_helper(&flow->uh, us, LAYER_DTLS, NULL,
				  recv_handler, flow);
	if (err)
		goto out;

	flow->ssl = SSL_new(tls->ctx);
	if (!flow->ssl)
		goto out;

	flow->sbio_in = BIO_new(BIO_s_mem());
	if (!flow->sbio_in)
		goto out;

	flow->sbio_out = BIO_new(&bio_udp_send);
	if (!flow->sbio_out) {
		BIO_free(flow->sbio_in);
		goto out;
	}
	flow->sbio_out->ptr = flow;

	SSL_set_bio(flow->ssl, flow->sbio_in, flow->sbio_out);

	tmr_init(&flow->tmr);

	err = 0;

 out:
	if (err)
		mem_deref(flow);
	else
		*flowp = flow;

	return err;
}


int dtls_flow_start(struct dtls_flow *flow, const struct sa *peer, bool active)
{
	int r, err = 0;

	if (!flow || !peer)
		return EINVAL;

	flow->peer = *peer;

	if (active) {
		r = SSL_connect(flow->ssl);
		if (r < 0) {
			int ssl_err = SSL_get_error(flow->ssl, r);

			ERR_clear_error();

			if (ssl_err != SSL_ERROR_WANT_READ) {
				DEBUG_WARNING("SSL_connect() failed"
					      " (err=%d)\n", ssl_err);
			}
		}

		check_timer(flow);
	}
	else {
		SSL_set_accept_state(flow->ssl);

		SSL_set_verify_depth(flow->ssl, 0);
		SSL_set_verify(flow->ssl,
			       SSL_VERIFY_PEER|SSL_VERIFY_CLIENT_ONCE,
			       verify_callback);
	}

	return err;
}


static const EVP_MD *type2evp(const char *type)
{
	if (0 == str_casecmp(type, "SHA-1"))
		return EVP_sha1();
	else if (0 == str_casecmp(type, "SHA-256"))
		return EVP_sha256();
	else
		return NULL;
}


int dtls_get_remote_fingerprint(const struct dtls_flow *flow, const char *type,
				struct tls_fingerprint *fp)
{
	X509 *x;

	if (!flow || !fp)
		return EINVAL;

	x = SSL_get_peer_certificate(flow->ssl);
	if (!x)
		return EPROTO;

	fp->len = sizeof(fp->md);
	if (1 != X509_digest(x, type2evp(type), fp->md, &fp->len))
		return ENOENT;

	return 0;
}
