/**
 * @file dtls_srtp/srtp.c Secure RTP
 *
 * Copyright (C) 2010 Creytiv.com
 */

#if defined (__GNUC__) && !defined (asm)
#define asm __asm__  /* workaround */
#endif
#include <srtp/srtp.h>
#include <re.h>
#include <baresip.h>
#include "dtls_srtp.h"


#define DEBUG_MODULE "dtls_srtp"
#define DEBUG_LEVEL 5
#include <re_dbg.h>


struct srtp_stream {
	srtp_policy_t policy;
	srtp_t srtp;
	uint8_t key[SRTP_MAX_KEY_LEN];
};


/*
 * See RFC 5764 figure 3:
 *
 *                  +----------------+
 *                  | 127 < B < 192 -+--> forward to RTP
 *                  |                |
 *      packet -->  |  19 < B < 64  -+--> forward to DTLS
 *                  |                |
 *                  |       B < 2   -+--> forward to STUN
 *                  +----------------+
 *
 */
static inline bool is_rtp_or_rtcp(const struct mbuf *mb)
{
	uint8_t b;

	if (mbuf_get_left(mb) < 1)
		return false;

	b = mbuf_buf(mb)[0];

	return 127 < b && b < 192;
}


static inline bool is_rtcp_packet(const struct mbuf *mb)
{
	uint8_t pt;

	if (mbuf_get_left(mb) < 2)
		return false;

	pt = mbuf_buf(mb)[1] & 0x7f;

	return 64 <= pt && pt <= 95;
}


static int errstatus_print(struct re_printf *pf, err_status_t e)
{
	const char *s;

	switch (e) {

	case err_status_ok:          s = "ok";          break;
	case err_status_fail:        s = "fail";        break;
	case err_status_auth_fail:   s = "auth_fail";   break;
	case err_status_cipher_fail: s = "cipher_fail"; break;
	case err_status_replay_fail: s = "replay_fail"; break;

	default:
		return re_hprintf(pf, "err=%d", e);
	}

	return re_hprintf(pf, "%s", s);
}


static void destructor(void *arg)
{
	struct srtp_stream *s = arg;

	if (s->srtp)
		srtp_dealloc(s->srtp);
}


static bool send_handler(int *err, struct sa *dst, struct mbuf *mb, void *arg)
{
	struct sock *sock = arg;
	err_status_t e;
	int len;
	(void)dst;

	if (!is_rtp_or_rtcp(mb))
		return false;

	len = (int)mbuf_get_left(mb);

	if (mbuf_get_space(mb) < ((size_t)len + SRTP_MAX_TRAILER_LEN)) {
		*err = mbuf_resize(mb, mb->pos + len + SRTP_MAX_TRAILER_LEN);
		if (*err)
			return true;
	}

	if (is_rtcp_packet(mb)) {
		e = srtp_protect_rtcp(sock->tx->srtp, mbuf_buf(mb), &len);
	}
	else {
		e = srtp_protect(sock->tx->srtp, mbuf_buf(mb), &len);
	}

	if (err_status_ok != e) {
		DEBUG_WARNING("send: failed to protect %s-packet"
			      " with %d bytes (%H)\n",
			      is_rtcp_packet(mb) ? "RTCP" : "RTP",
			      len, errstatus_print, e);
		*err = EPROTO;
		return false;
	}

	mbuf_set_end(mb, mb->pos + len);

	return false;  /* continue processing */
}


static bool recv_handler(struct sa *src, struct mbuf *mb, void *arg)
{
	struct sock *sock = arg;
	err_status_t e;
	int len;
	(void)src;

	if (!is_rtp_or_rtcp(mb))
		return false;

	len = (int)mbuf_get_left(mb);

	if (is_rtcp_packet(mb)) {
		e = srtp_unprotect_rtcp(sock->rx->srtp, mbuf_buf(mb), &len);
	}
	else {
		e = srtp_unprotect(sock->rx->srtp, mbuf_buf(mb), &len);
	}

	if (e != err_status_ok) {
		DEBUG_WARNING("recv: failed to unprotect %s-packet"
			      " with %d bytes (%H)\n",
			      is_rtcp_packet(mb) ? "RTCP" : "RTP",
			      len, errstatus_print, e);
		return true;   /* error - drop packet */
	}

	mbuf_set_end(mb, mb->pos + len);

	return false;  /* continue processing */
}


int srtp_stream_add(struct srtp_stream **sp, const char *profile,
		    const struct key *key, bool tx)
{
	struct srtp_stream *s;
	err_status_t e;
	int err = 0;

	if (!sp || !key || key->key_len > SRTP_MAX_KEY_LEN)
		return EINVAL;

	s = mem_zalloc(sizeof(*s), destructor);
	if (!s)
		return ENOMEM;

	memcpy(s->key, key->key, key->key_len);
	append_salt_to_key(s->key, (unsigned int)key->key_len,
			   (unsigned char *)key->salt,
			   (unsigned int)key->salt_len);

	/* note: policy and key must be on the heap */

	if (0 == str_casecmp(profile, "SRTP_AES128_CM_SHA1_80")) {
		crypto_policy_set_aes_cm_128_hmac_sha1_80(&s->policy.rtp);
		crypto_policy_set_aes_cm_128_hmac_sha1_80(&s->policy.rtcp);
	}
	else if (0 == str_casecmp(profile, "SRTP_AES128_CM_SHA1_32")) {
		crypto_policy_set_aes_cm_128_hmac_sha1_32(&s->policy.rtp);
		crypto_policy_set_aes_cm_128_hmac_sha1_32(&s->policy.rtcp);
	}
	else {
		DEBUG_WARNING("unsupported profile: %s\n", profile);
		err = ENOSYS;
		goto out;
	}

	s->policy.ssrc.type = tx ? ssrc_any_outbound : ssrc_any_inbound;
	s->policy.key       = s->key;
	s->policy.next      = NULL;

	e = srtp_create(&s->srtp, &s->policy);
	if (err_status_ok != e) {
		s->srtp = NULL;
		DEBUG_WARNING("srtp_create() failed. e=%d\n", e);
		err = ENOMEM;
		goto out;
	}

 out:
	if (err)
		mem_deref(s);
	else
		*sp = s;

	return err;
}


int srtp_install(struct sock *sock)
{
	return udp_register_helper(&sock->uh_srtp, sock->app_sock,
				   LAYER_SRTP,
				   send_handler,
				   recv_handler,
				   sock);
}
