/**
 * @file srtp.h  GNU ZRTP: SRTP processing
 *
 * Copyright (C) 2010 - 2017 Creytiv.com
 */
#ifndef __SRTP_H
#define __SRTP_H


#include <libzrtpcpp/ZrtpCallback.h>


#ifdef GZRTP_USE_RE_SRTP
struct srtp;
#else
class CryptoContext;
class CryptoContextCtrl;
#endif


class Srtp {
public:
	Srtp(int& err, const SrtpSecret_t *secrets, EnableSecurity part);
	~Srtp();

	int protect(struct mbuf *mb);
	int protect_ctrl(struct mbuf *mb);
	int unprotect(struct mbuf *mb);
	int unprotect_ctrl(struct mbuf *mb);

private:
	int protect_int(struct mbuf *mb, bool control);
	int unprotect_int(struct mbuf *mb, bool control);

#ifdef GZRTP_USE_RE_SRTP
	int32_t m_auth_tag_len;
	struct srtp *m_srtp;
#else
	CryptoContext *m_cc;
	CryptoContextCtrl *m_cc_ctrl;
#endif
};


#endif // __SRTP_H

