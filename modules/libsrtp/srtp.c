/**
 * @file modules/srtp/srtp.c Secure Real-time Transport Protocol (RFC 3711)
 *
 * Copyright (C) 2010 Creytiv.com
 */
#if defined (__GNUC__) && !defined (asm)
#define asm __asm__  /* workaround */
#endif
#include <srtp/srtp.h>
#include <srtp/crypto_kernel.h>
#include <re.h>
#include <baresip.h>
#include "sdes.h"


/*
 * NOTE: this module is deprecated, please use the 'srtp' module instead.
 */


struct menc_st {
	/* one SRTP session per media line */
	uint8_t key_tx[32];  /* 32 for alignment, only 30 used */
	uint8_t key_rx[32];
	srtp_t srtp_tx, srtp_rx;
	srtp_policy_t policy_tx, policy_rx;
	bool use_srtp;
	char *crypto_suite;

	void *rtpsock;
	void *rtcpsock;
	struct udp_helper *uh_rtp;   /**< UDP helper for RTP encryption    */
	struct udp_helper *uh_rtcp;  /**< UDP helper for RTCP encryption   */
	struct sdp_media *sdpm;
};


static const char aes_cm_128_hmac_sha1_32[] = "AES_CM_128_HMAC_SHA1_32";
static const char aes_cm_128_hmac_sha1_80[] = "AES_CM_128_HMAC_SHA1_80";


static void destructor(void *arg)
{
	struct menc_st *st = arg;

	mem_deref(st->sdpm);
	mem_deref(st->crypto_suite);

	/* note: must be done before freeing socket */
	mem_deref(st->uh_rtp);
	mem_deref(st->uh_rtcp);
	mem_deref(st->rtpsock);
	mem_deref(st->rtcpsock);

	if (st->srtp_tx)
		srtp_dealloc(st->srtp_tx);
	if (st->srtp_rx)
		srtp_dealloc(st->srtp_rx);
}


static bool cryptosuite_issupported(const struct pl *suite)
{
	if (0 == pl_strcasecmp(suite, aes_cm_128_hmac_sha1_32)) return true;
	if (0 == pl_strcasecmp(suite, aes_cm_128_hmac_sha1_80)) return true;

	return false;
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
static bool is_rtp_or_rtcp(const struct mbuf *mb)
{
	uint8_t b;

	if (mbuf_get_left(mb) < 1)
		return false;

	b = mbuf_buf(mb)[0];

	return 127 < b && b < 192;
}


static bool is_rtcp_packet(const struct mbuf *mb)
{
	uint8_t pt;

	if (mbuf_get_left(mb) < 2)
		return false;

	pt = mbuf_buf(mb)[1] & 0x7f;

	return 64 <= pt && pt <= 95;
}


static int start_srtp(struct menc_st *st, const char *suite)
{
	crypto_policy_t policy;
	err_status_t e;

	if (0 == str_casecmp(suite, aes_cm_128_hmac_sha1_32)) {
		crypto_policy_set_aes_cm_128_hmac_sha1_32(&policy);
	}
	else if (0 == str_casecmp(suite, aes_cm_128_hmac_sha1_80)) {
		crypto_policy_set_aes_cm_128_hmac_sha1_80(&policy);
	}
	else {
		warning("srtp: unknown SRTP crypto suite (%s)\n", suite);
		return ENOENT;
	}

	/* transmit policy */
	st->policy_tx.rtp = policy;
	st->policy_tx.rtcp = policy;
	st->policy_tx.ssrc.type = ssrc_any_outbound;
	st->policy_tx.key = st->key_tx;
	st->policy_tx.next = NULL;

	/* receive policy */
	st->policy_rx.rtp = policy;
	st->policy_rx.rtcp = policy;
	st->policy_rx.ssrc.type = ssrc_any_inbound;
	st->policy_rx.key = st->key_rx;
	st->policy_rx.next = NULL;

	/* allocate and initialize the SRTP session */
	e = srtp_create(&st->srtp_tx, &st->policy_tx);
	if (e != err_status_ok) {
		warning("srtp: srtp_create TX failed (%H)\n",
			errstatus_print, e);
		return EPROTO;
	}

	e = srtp_create(&st->srtp_rx, &st->policy_rx);
	if (err_status_ok != e) {
		warning("srtp: srtp_create RX failed (%H)\n",
			errstatus_print, e);
		return EPROTO;
	}

	/* use SRTP for this stream/session */
	st->use_srtp = true;

	return 0;
}


static int setup_srtp(struct menc_st *st)
{
	err_status_t e;

	/* init SRTP */
	e = crypto_get_random(st->key_tx, SRTP_MASTER_KEY_LEN);
	if (err_status_ok != e) {
		warning("srtp: crypto_get_random() failed (%H)\n",
			errstatus_print, e);
		return ENOSYS;
	}

	return 0;
}


static bool send_handler(int *err, struct sa *dst, struct mbuf *mb, void *arg)
{
	struct menc_st *st = arg;
	err_status_t e;
	int len;
	(void)dst;

	if (!st->use_srtp || !is_rtp_or_rtcp(mb))
		return false;

	len = (int)mbuf_get_left(mb);

	if (mbuf_get_space(mb) < ((size_t)len + SRTP_MAX_TRAILER_LEN)) {
		mbuf_resize(mb, mb->pos + len + SRTP_MAX_TRAILER_LEN);
	}

	if (is_rtcp_packet(mb)) {
		e = srtp_protect_rtcp(st->srtp_tx, mbuf_buf(mb), &len);
	}
	else {
		e = srtp_protect(st->srtp_tx, mbuf_buf(mb), &len);
	}

	if (err_status_ok != e) {
		warning("srtp: send: failed to protect %s-packet"
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
	struct menc_st *st = arg;
	err_status_t e;
	int len;
	(void)src;

	if (!st->use_srtp || !is_rtp_or_rtcp(mb))
		return false;

	len = (int)mbuf_get_left(mb);

	if (is_rtcp_packet(mb)) {
		e = srtp_unprotect_rtcp(st->srtp_rx, mbuf_buf(mb), &len);
	}
	else {
		e = srtp_unprotect(st->srtp_rx, mbuf_buf(mb), &len);
	}

	if (e != err_status_ok) {
		warning("srtp: recv: failed to unprotect %s-packet"
			" with %d bytes (%H)\n",
			is_rtcp_packet(mb) ? "RTCP" : "RTP",
			len, errstatus_print, e);
		return true;   /* error - drop packet */
	}

	mbuf_set_end(mb, mb->pos + len);

	return false;  /* continue processing */
}


/* a=crypto:<tag> <crypto-suite> <key-params> [<session-params>] */
static int sdp_enc(struct menc_st *st, struct sdp_media *m,
		   uint32_t tag, const char *suite)
{
	char key[128] = "";
	size_t olen;
	int err;

	olen = sizeof(key);
	err = base64_encode(st->key_tx, SRTP_MASTER_KEY_LEN, key, &olen);
	if (err)
		return err;

	return libsrtp_sdes_encode_crypto(m, tag, suite, key, olen);
}


static int start_crypto(struct menc_st *st, const struct pl *key_info)
{
	size_t olen;
	int err;

	/* key-info is BASE64 encoded */

	olen = sizeof(st->key_rx);
	err = base64_decode(key_info->p, key_info->l, st->key_rx, &olen);
	if (err)
		return err;

	if (SRTP_MASTER_KEY_LEN != olen) {
		warning("srtp: srtp keylen is %u (should be 30)\n", olen);
	}

	err = start_srtp(st, st->crypto_suite);
	if (err)
		return err;

	info("srtp: %s: SRTP is Enabled (cryptosuite=%s)\n",
	     sdp_media_name(st->sdpm), st->crypto_suite);

	return 0;
}


static bool sdp_attr_handler(const char *name, const char *value, void *arg)
{
	struct menc_st *st = arg;
	struct crypto c;
	(void)name;

	if (libsrtp_sdes_decode_crypto(&c, value))
		return false;

	if (0 != pl_strcmp(&c.key_method, "inline"))
		return false;

	if (!cryptosuite_issupported(&c.suite))
		return false;

	st->crypto_suite = mem_deref(st->crypto_suite);
	pl_strdup(&st->crypto_suite, &c.suite);

	if (start_crypto(st, &c.key_info))
		return false;

	sdp_enc(st, st->sdpm, c.tag, st->crypto_suite);

	return true;
}


static int alloc(struct menc_media **stp, struct menc_sess *sess,
		 struct rtp_sock *rtp,
		 int proto, void *rtpsock, void *rtcpsock,
		 struct sdp_media *sdpm)
{
	struct menc_st *st;
	const char *rattr = NULL;
	int layer = 10; /* above zero */
	int err = 0;
	bool mux = (rtpsock == rtcpsock);
	(void)sess;
	(void)rtp;

	if (!stp || !sdpm)
		return EINVAL;
	if (proto != IPPROTO_UDP)
		return EPROTONOSUPPORT;

	st = (struct menc_st *)*stp;
	if (!st) {

		st = mem_zalloc(sizeof(*st), destructor);
		if (!st)
			return ENOMEM;

		st->sdpm = mem_ref(sdpm);

		err = sdp_media_set_alt_protos(st->sdpm, 4,
					       "RTP/AVP",
					       "RTP/AVPF",
					       "RTP/SAVP",
					       "RTP/SAVPF");
		if (err)
			goto out;

		if (rtpsock) {
			st->rtpsock = mem_ref(rtpsock);
			err |= udp_register_helper(&st->uh_rtp, rtpsock,
						   layer, send_handler,
						   recv_handler, st);
		}
		if (rtcpsock && !mux) {
			st->rtcpsock = mem_ref(rtcpsock);
			err |= udp_register_helper(&st->uh_rtcp, rtcpsock,
						   layer, send_handler,
						   recv_handler, st);
		}
		if (err)
			goto out;

		/* set our preferred crypto-suite */
		err |= str_dup(&st->crypto_suite, aes_cm_128_hmac_sha1_80);
		if (err)
			goto out;

		err = setup_srtp(st);
		if (err)
			goto out;
	}

	/* SDP handling */

	if (sdp_media_rattr(st->sdpm, "crypto")) {

		rattr = sdp_media_rattr_apply(st->sdpm, "crypto",
					      sdp_attr_handler, st);
		if (!rattr) {
			warning("srtp: no valid a=crypto attribute from"
				" remote peer\n");
		}
	}

	if (!rattr)
		err = sdp_enc(st, sdpm, 0, st->crypto_suite);

 out:
	if (err)
		mem_deref(st);
	else
		*stp = (struct menc_media *)st;

	return err;
}


static struct menc menc_srtp_opt = {
	LE_INIT, "srtp", "RTP/AVP", NULL, alloc
};

static struct menc menc_srtp_mand = {
	LE_INIT, "srtp-mand", "RTP/SAVP", NULL, alloc
};

static struct menc menc_srtp_mandf = {
	LE_INIT, "srtp-mandf", "RTP/SAVPF", NULL, alloc
};


static int mod_srtp_init(void)
{
	struct list *mencl = baresip_mencl();
	err_status_t err;

	err = srtp_init();
	if (err_status_ok != err) {
		warning("srtp: srtp_init() failed (%H)\n",
			errstatus_print, err);
		return ENOSYS;
	}

	menc_register(mencl, &menc_srtp_opt);
	menc_register(mencl, &menc_srtp_mand);
	menc_register(mencl, &menc_srtp_mandf);

	return 0;
}


static int mod_srtp_close(void)
{
	menc_unregister(&menc_srtp_mandf);
	menc_unregister(&menc_srtp_mand);
	menc_unregister(&menc_srtp_opt);

	crypto_kernel_shutdown();

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(libsrtp) = {
	"libsrtp",
	"menc",
	mod_srtp_init,
	mod_srtp_close
};
