/**
 * @file srtp.cpp  GNU ZRTP: SRTP processing
 *
 * Copyright (C) 2010 - 2017 Creytiv.com
 */
#include <stdint.h>

#include <re.h>
#include <baresip.h>

#ifdef GZRTP_USE_RE_SRTP
#include <string.h>
#else
#include <srtp/CryptoContext.h>
#include <srtp/CryptoContextCtrl.h>
#include <srtp/SrtpHandler.h>
#endif

#include "srtp.h"


Srtp::Srtp(int& err, const SrtpSecret_t *secrets, EnableSecurity part)

{
	const uint8_t *key, *salt;
	uint32_t key_len, salt_len;

	err = EPERM;

#ifdef GZRTP_USE_RE_SRTP
	m_srtp = NULL;
#else
	m_cc = NULL;
	m_cc_ctrl = NULL;
#endif

	if (part == ForSender) {
		// To encrypt packets: intiator uses initiator keys,
		// responder uses responder keys
		if (secrets->role == Initiator) {
			key = secrets->keyInitiator;
			key_len = secrets->initKeyLen / 8;
			salt = secrets->saltInitiator;
			salt_len = secrets->initSaltLen / 8;
		}
		else {
			key = secrets->keyResponder;
			key_len = secrets->respKeyLen / 8;
			salt = secrets->saltResponder;
			salt_len = secrets->respSaltLen / 8;
		}
	}
	else if (part == ForReceiver) {
		// To decrypt packets: intiator uses responder keys,
		// responder initiator keys
		if (secrets->role == Initiator) {
			key = secrets->keyResponder;
			key_len = secrets->respKeyLen / 8;
			salt = secrets->saltResponder;
			salt_len = secrets->respSaltLen / 8;
		}
		else {
			key = secrets->keyInitiator;
			key_len = secrets->initKeyLen / 8;
			salt = secrets->saltInitiator;
			salt_len = secrets->initSaltLen / 8;
		}
	}
	else {
		err = EINVAL;
		return;
	}

#ifdef GZRTP_USE_RE_SRTP

	uint8_t key_buf[32 + 14];  // max key + salt
	enum srtp_suite suite;
	struct srtp *st;

	if (secrets->symEncAlgorithm == Aes &&
	    secrets->authAlgorithm == Sha1) {

		if (key_len == 16 && secrets->srtpAuthTagLen == 32)
			suite = SRTP_AES_CM_128_HMAC_SHA1_32;

		else if (key_len == 16 && secrets->srtpAuthTagLen == 80)
			suite = SRTP_AES_CM_128_HMAC_SHA1_80;

		else if (key_len == 32 && secrets->srtpAuthTagLen == 32)
			suite = SRTP_AES_256_CM_HMAC_SHA1_32;

		else if (key_len == 32 && secrets->srtpAuthTagLen == 80)
			suite = SRTP_AES_256_CM_HMAC_SHA1_80;

		else {
			err = ENOTSUP;
			return;
		}
	}
	else {
		err = ENOTSUP;
		return;
	}

	if (salt_len != 14) {
		err = EINVAL;
		return;
	}

	memcpy(key_buf, key, key_len);
	memcpy(key_buf + key_len, salt, salt_len);

	err = srtp_alloc(&st, suite, key_buf, key_len + salt_len, 0);
	if (err)
		return;

	m_auth_tag_len = secrets->srtpAuthTagLen / 8;
	m_srtp = st;

	err = 0;
#else

	CryptoContext *cc = NULL;
	CryptoContextCtrl *cc_ctrl = NULL;
	int cipher;
	int authn;
	int auth_key_len;

	switch (secrets->authAlgorithm) {
	case Sha1:
		authn = SrtpAuthenticationSha1Hmac;
		auth_key_len = 20;
		break;
	case Skein:
		authn = SrtpAuthenticationSkeinHmac;
		auth_key_len = 32;
		break;
	default:
		err = ENOTSUP;
		return;
	}

	switch (secrets->symEncAlgorithm) {
	case Aes:
		cipher = SrtpEncryptionAESCM;
		break;
	case TwoFish:
		cipher = SrtpEncryptionTWOCM;
		break;
	default:
		err = ENOTSUP;
		return;
	}

	cc = new CryptoContext(
	        0,                             // SSRC (used for lookup)
	        0,                             // Roll-Over-Counter (ROC)
	        0L,                            // keyderivation << 48,
	        cipher,                        // encryption algo
	        authn,                         // authtentication algo
	        (uint8_t *)key,                // Master Key
	        key_len,                       // Master Key length
	        (uint8_t *)salt,               // Master Salt
	        salt_len,                      // Master Salt length
	        key_len,                       // encryption keyl
	        auth_key_len,                  // authentication key len
	        salt_len,                      // session salt len
	        secrets->srtpAuthTagLen / 8);  // authentication tag lenA

	cc_ctrl = new CryptoContextCtrl(
	        0,                             // SSRC (used for lookup)
	        cipher,                        // encryption algo
	        authn,                         // authtentication algo
	        (uint8_t *)key,                // Master Key
	        key_len,                       // Master Key length
	        (uint8_t *)salt,               // Master Salt
	        salt_len,                      // Master Salt length
	        key_len,                       // encryption keyl
	        auth_key_len,                  // authentication key len
	        salt_len,                      // session salt len
	        secrets->srtpAuthTagLen / 8);  // authentication tag lenA

	if (!cc || !cc_ctrl) {
		delete cc;
		delete cc_ctrl;

		err = ENOMEM;
		return;
	}

	cc->deriveSrtpKeys(0L);
	cc_ctrl->deriveSrtcpKeys();

	m_cc = cc;
	m_cc_ctrl = cc_ctrl;

	err = 0;
#endif
}


Srtp::~Srtp()
{
#ifdef GZRTP_USE_RE_SRTP
	mem_deref(m_srtp);
#else
	delete m_cc;
	delete m_cc_ctrl;
#endif
}


int Srtp::protect_int(struct mbuf *mb, bool control)
{
	size_t len = mbuf_get_left(mb);

	int32_t extra = (mbuf_get_space(mb) > len)?
	                 mbuf_get_space(mb) - len : 0;

#ifdef GZRTP_USE_RE_SRTP
	if (m_auth_tag_len + (control? 4 : 0) > extra)
		return ENOMEM;

	if (control)
		return srtcp_encrypt(m_srtp, mb);
	else
		return srtp_encrypt(m_srtp, mb);
#else
	if (control) {
		if (m_cc_ctrl->getTagLength() + 4 +
		    m_cc_ctrl->getMkiLength() > extra)
			return ENOMEM;
	}
	else {
		if (m_cc->getTagLength() +
		    m_cc->getMkiLength() > extra)
			return ENOMEM;
	}

	bool rc;

	if (control)
		rc = SrtpHandler::protectCtrl(m_cc_ctrl, mbuf_buf(mb),
		                              len, &len);
	else
		rc = SrtpHandler::protect(m_cc, mbuf_buf(mb), len, &len);
	if (!rc)
		return EPROTO;

	if (len > mbuf_get_space(mb)) {
		// this should never happen
		warning("zrtp: protect: length > space (%u > %u)\n",
			len, mbuf_get_space(mb));
		abort();
	}

	mb->end = mb->pos + len;

	return 0;
#endif
}


int Srtp::protect(struct mbuf *mb)
{
	return protect_int(mb, false);
}


int Srtp::protect_ctrl(struct mbuf *mb)
{
	return protect_int(mb, true);
}


// return value:
// 0        - OK
// EBADMSG  - SRTP/RTP packet decode error
// EAUTH    - SRTP authentication failed
// EALREADY - SRTP replay check failed
// other errors
int Srtp::unprotect_int(struct mbuf *mb, bool control)
{
#ifdef GZRTP_USE_RE_SRTP
	if (control)
		return srtcp_decrypt(m_srtp, mb);
	else
		return srtp_decrypt(m_srtp, mb);
#else
	size_t len = mbuf_get_left(mb);
	int rc, err;

	if (control)
		rc = SrtpHandler::unprotectCtrl(m_cc_ctrl, mbuf_buf(mb),
		                                len, &len);
	else
		rc = SrtpHandler::unprotect(m_cc, mbuf_buf(mb),
		                            len, &len, NULL);

	switch (rc) {
	case  1:  err = 0;        break;
	case  0:  err = EBADMSG;  break;
	case -1:  err = EAUTH;    break;
	case -2:  err = EALREADY; break;
	default:  err = EINVAL;
	}

	if (!err)
		mb->end = mb->pos + len;

	return err;
#endif
}


int Srtp::unprotect(struct mbuf *mb)
{
	return unprotect_int(mb, false);
}


int Srtp::unprotect_ctrl(struct mbuf *mb)
{
	return unprotect_int(mb, true);
}

