/**
 * @file dtls.c DTLS functions
 *
 * Copyright (C) 2010 Creytiv.com
 */

#include <re.h>
#include <baresip.h>
#include "dtls_srtp.h"


int dtls_print_sha256_fingerprint(struct re_printf *pf, const struct tls *tls)
{
	uint8_t md[32];
	unsigned int i;
	int err = 0;

	if (!tls)
		return EINVAL;

	err = tls_fingerprint(tls, TLS_FINGERPRINT_SHA256, md, sizeof(md));
	if (err)
		return err;

	for (i=0; i<sizeof(md); i++) {
		err |= re_hprintf(pf, "%s%02X", i==0 ? "" : ":", md[i]);
	}

	return err;
}
