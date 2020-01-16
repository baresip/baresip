/**
 * @file modules/srtp/srtp.c Secure Real-time Transport Protocol (RFC 3711)
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <re.h>
#include <baresip.h>
#include "sdes.h"


/**
 * @defgroup srtp srtp
 *
 * Secure Real-time Transport Protocol module
 *
 * This module implements media encryption using SRTP and SDES.
 *
 * SRTP can be enabled in ~/.baresip/accounts:
 *
 \verbatim
  <sip:user@domain.com>;mediaenc=srtp
  <sip:user@domain.com>;mediaenc=srtp-mand
 \endverbatim
 *
 */


struct menc_sess {
	menc_event_h *eventh;
	void *arg;
};


struct menc_st {
	/* one SRTP session per media line */
	const struct menc_sess *sess;
	uint8_t key_tx[32+12];
	/* base64_decoding worst case encoded 32+12 key */
	uint8_t key_rx[46];
	struct srtp *srtp_tx, *srtp_rx;
	bool use_srtp;
	bool got_sdp;
	char *crypto_suite;

	void *rtpsock;
	void *rtcpsock;
	struct udp_helper *uh_rtp;   /**< UDP helper for RTP encryption    */
	struct udp_helper *uh_rtcp;  /**< UDP helper for RTCP encryption   */
	struct sdp_media *sdpm;
	const struct stream *strm;   /**< pointer to parent */
};


static const char aes_cm_128_hmac_sha1_32[] = "AES_CM_128_HMAC_SHA1_32";
static const char aes_cm_128_hmac_sha1_80[] = "AES_CM_128_HMAC_SHA1_80";
static const char aes_128_gcm[]             = "AEAD_AES_128_GCM";
static const char aes_256_gcm[]             = "AEAD_AES_256_GCM";

static const char *preferred_suite = aes_cm_128_hmac_sha1_80;


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

	mem_deref(st->srtp_tx);
	mem_deref(st->srtp_rx);
}


static bool cryptosuite_issupported(const struct pl *suite)
{
	if (0 == pl_strcasecmp(suite, aes_cm_128_hmac_sha1_32)) return true;
	if (0 == pl_strcasecmp(suite, aes_cm_128_hmac_sha1_80)) return true;
	if (0 == pl_strcasecmp(suite, aes_128_gcm))             return true;
	if (0 == pl_strcasecmp(suite, aes_256_gcm))             return true;

	return false;
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


static enum srtp_suite resolve_suite(const char *suite)
{
	if (0 == str_casecmp(suite, aes_cm_128_hmac_sha1_32))
		return SRTP_AES_CM_128_HMAC_SHA1_32;
	if (0 == str_casecmp(suite, aes_cm_128_hmac_sha1_80))
		return SRTP_AES_CM_128_HMAC_SHA1_80;
	if (0 == str_casecmp(suite, aes_128_gcm))
		return SRTP_AES_128_GCM;
	if (0 == str_casecmp(suite, aes_256_gcm))
		return SRTP_AES_256_GCM;

	return -1;
}


static size_t get_master_keylen(enum srtp_suite suite)
{
	switch (suite) {

	case SRTP_AES_CM_128_HMAC_SHA1_32: return 16+14;
	case SRTP_AES_CM_128_HMAC_SHA1_80: return 16+14;
	case SRTP_AES_128_GCM:             return 16+12;
	case SRTP_AES_256_GCM:             return 32+12;
	default: return 0;
	}
}


static int start_srtp(struct menc_st *st, const char *suite_name)
{
	enum srtp_suite suite;
	size_t len;
	int err;

	suite = resolve_suite(suite_name);

	len = get_master_keylen(suite);

	/* allocate and initialize the SRTP session */
	if (!st->srtp_tx) {
		err = srtp_alloc(&st->srtp_tx, suite, st->key_tx, len, 0);
		if (err) {
			warning("srtp: srtp_alloc TX failed (%m)\n", err);
			return err;
		}
	}

	if (!st->srtp_rx) {
		err = srtp_alloc(&st->srtp_rx, suite, st->key_rx, len, 0);
		if (err) {
			warning("srtp: srtp_alloc RX failed (%m)\n", err);
			return err;
		}
	}

	/* use SRTP for this stream/session */
	st->use_srtp = true;

	return 0;
}


static bool send_handler(int *err, struct sa *dst, struct mbuf *mb, void *arg)
{
	struct menc_st *st = arg;
	size_t len = mbuf_get_left(mb);
	int lerr = 0;
	(void)dst;

	if (!st->use_srtp || !is_rtp_or_rtcp(mb))
		return false;

	if (is_rtcp_packet(mb)) {
		lerr = srtcp_encrypt(st->srtp_tx, mb);
	}
	else {
		lerr = srtp_encrypt(st->srtp_tx, mb);
	}

	if (lerr) {
		warning("srtp: failed to encrypt %s-packet"
			      " with %zu bytes (%m)\n",
			      is_rtcp_packet(mb) ? "RTCP" : "RTP",
			      len, lerr);
		*err = lerr;
		return false;
	}

	return false;  /* continue processing */
}


static bool recv_handler(struct sa *src, struct mbuf *mb, void *arg)
{
	struct menc_st *st = arg;
	size_t len = mbuf_get_left(mb);
	int err = 0;
	(void)src;

	if (!st->got_sdp)
		return true;  /* drop the packet */

	if (!st->use_srtp || !is_rtp_or_rtcp(mb))
		return false;

	if (is_rtcp_packet(mb)) {
		err = srtcp_decrypt(st->srtp_rx, mb);
		if (err) {
			warning("srtp: failed to decrypt RTCP packet"
				" with %zu bytes (%m)\n", len, err);
		}
	}
	else {
		err = srtp_decrypt(st->srtp_rx, mb);
		if (err) {
			warning("srtp: failed to decrypt RTP packet"
				" with %zu bytes (%m)\n", len, err);
		}
	}

	return err ? true : false;
}


/* a=crypto:<tag> <crypto-suite> <key-params> [<session-params>] */
static int sdp_enc(struct menc_st *st, struct sdp_media *m,
		   uint32_t tag, const char *suite)
{
	char key[128] = "";
	size_t len, olen;
	int err;

	len = get_master_keylen(resolve_suite(suite));

	olen = sizeof(key);
	err = base64_encode(st->key_tx, len, key, &olen);
	if (err)
		return err;

	return sdes_encode_crypto(m, tag, suite, key, olen);
}


static int start_crypto(struct menc_st *st, const struct pl *key_info)
{
	size_t olen, len;
	char buf[64] = "";
	int err;

	len = get_master_keylen(resolve_suite(st->crypto_suite));

	/* key-info is BASE64 encoded */

	olen = sizeof(st->key_rx);
	err = base64_decode(key_info->p, key_info->l, st->key_rx, &olen);
	if (err)
		return err;

	if (len != olen) {
		warning("srtp: %s: srtp keylen is %u (should be %zu)\n",
			st->crypto_suite, olen, len);
	}

	err = start_srtp(st, st->crypto_suite);
	if (err)
		return err;

	info("srtp: %s: SRTP is Enabled (cryptosuite=%s)\n",
	     sdp_media_name(st->sdpm), st->crypto_suite);

	if (st->sess->eventh) {
		if (re_snprintf(buf, sizeof(buf), "%s,%s",
				sdp_media_name(st->sdpm),
				st->crypto_suite))
			st->sess->eventh(MENC_EVENT_SECURE, buf,
					 (struct stream *)st->strm,
					 st->sess->arg);
		else
			warning("srtp: failed to print secure"
				" event arguments\n");
	}

	return 0;
}


static bool sdp_attr_handler(const char *name, const char *value, void *arg)
{
	struct menc_st *st = arg;
	struct crypto c;
	(void)name;

	if (sdes_decode_crypto(&c, value))
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


static int session_alloc(struct menc_sess **sessp,
			 struct sdp_session *sdp, bool offerer,
			 menc_event_h *eventh, menc_error_h *errorh,
			 void *arg)
{
	struct menc_sess *sess;
	(void)sdp;
	(void)offerer;
	(void)errorh;

	if (!sessp)
		return EINVAL;

	sess = mem_zalloc(sizeof(*sess), NULL);
	if (!sess)
		return ENOMEM;

	sess->eventh  = eventh;
	sess->arg     = arg;

	*sessp = sess;

	return 0;
}


static int media_alloc(struct menc_media **stp, struct menc_sess *sess,
		 struct rtp_sock *rtp,
		 struct udp_sock *rtpsock, struct udp_sock *rtcpsock,
	         const struct sa *raddr_rtp,
	         const struct sa *raddr_rtcp,
		 struct sdp_media *sdpm, const struct stream *strm)
{
	struct menc_st *st;
	const char *rattr = NULL;
	int layer = 10; /* above zero */
	int err = 0;
	bool mux = (rtpsock == rtcpsock);
	(void)sess;
	(void)rtp;
	(void)raddr_rtp;
	(void)raddr_rtcp;

	if (!stp || !sdpm || !sess)
		return EINVAL;

	st = (struct menc_st *)*stp;
	if (!st) {

		st = mem_zalloc(sizeof(*st), destructor);
		if (!st)
			return ENOMEM;

		st->sess = sess;
		st->sdpm = mem_ref(sdpm);
		st->strm = strm;

		if (0 == str_cmp(sdp_media_proto(sdpm), "RTP/AVP")) {
			err = sdp_media_set_alt_protos(st->sdpm, 4,
						       "RTP/AVP",
						       "RTP/AVPF",
						       "RTP/SAVP",
						       "RTP/SAVPF");
			if (err)
				goto out;
		}

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
		err |= str_dup(&st->crypto_suite, preferred_suite);
		if (err)
			goto out;

		rand_bytes(st->key_tx, sizeof(st->key_tx));
	}

	/* SDP handling */

	if (sdp_media_rport(sdpm))
		st->got_sdp = true;

	if (sdp_media_rattr(st->sdpm, "crypto")) {

		rattr = sdp_media_rattr_apply(st->sdpm, "crypto",
					      sdp_attr_handler, st);
		if (!rattr) {
			warning("srtp: no valid a=crypto attribute from"
				" remote peer\n");
		}
	}

	if (!rattr)
		err = sdp_enc(st, sdpm, 1, st->crypto_suite);

 out:
	if (err)
		mem_deref(st);
	else
		*stp = (struct menc_media *)st;

	return err;
}


static struct menc menc_srtp_opt = {
	.id        = "srtp",
	.sdp_proto = "RTP/AVP",
	.sessh     = session_alloc,
	.mediah    = media_alloc
};

static struct menc menc_srtp_mand = {
	.id        = "srtp-mand",
	.sdp_proto = "RTP/SAVP",
	.sessh     = session_alloc,
	.mediah    = media_alloc
};

static struct menc menc_srtp_mandf = {
	.id        = "srtp-mandf",
	.sdp_proto = "RTP/SAVPF",
	.sessh     = session_alloc,
	.mediah    = media_alloc
};


static int mod_srtp_init(void)
{
	struct list *mencl = baresip_mencl();

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

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(srtp) = {
	"srtp",
	"menc",
	mod_srtp_init,
	mod_srtp_close
};
