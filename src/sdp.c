/**
 * @file src/sdp.c  SDP functions
 *
 * Copyright (C) 2011 Creytiv.com
 */
#include <stdlib.h>
#include <re.h>
#include <baresip.h>
#include "core.h"


/**
 * Decode an SDP fingerprint value
 *
 * @param attr SDP attribute value
 * @param hash Returned hash method
 * @param md   Returned message digest
 * @param sz   Message digest size, set on return
 *
 * @return 0 if success, otherwise errorcode
 *
 * Reference: RFC 4572
 */
int sdp_fingerprint_decode(const char *attr, struct pl *hash,
			   uint8_t *md, size_t *sz)
{
	struct pl f;
	const char *p;
	int err;

	if (!attr || !hash)
		return EINVAL;

	err = re_regex(attr, str_len(attr), "[^ ]+ [0-9A-F:]+", hash, &f);
	if (err)
		return err;

	if (md && sz) {
		if (*sz < (f.l+1)/3)
			return EOVERFLOW;

		for (p = f.p; p < (f.p+f.l); p += 3) {
			*md++ = ch_hex(p[0]) << 4 | ch_hex(p[1]);
		}

		*sz = (f.l+1)/3;
	}

	return 0;
}


/**
 * Check if an SDP media object has valid media. It is considered
 * valid if it has one or more codecs, and the port number is set.
 *
 * @param m SDP Media object
 *
 * @return True if it has media, false if not
 */
bool sdp_media_has_media(const struct sdp_media *m)
{
	bool has;

	has = sdp_media_rformat(m, NULL) != NULL;
	if (has)
		return sdp_media_rport(m) != 0;

	return false;
}


static void decode_part(const struct pl *part, struct mbuf *mb)
{
	struct pl hdrs, body;

	if (re_regex(part->p, part->l, "\r\n\r\n[^]+", &body))
		return;

	hdrs.p = part->p;
	hdrs.l = body.p - part->p - 2;

	if (0 == re_regex(hdrs.p, hdrs.l, "application/sdp")) {

		mb->pos += (body.p - (char *)mbuf_buf(mb));
		mb->end  = mb->pos + body.l;
	}
}


/**
 * Decode a multipart/mixed message and find the part with application/sdp
 *
 * @param ctype_prm  Content type parameter
 * @param mb         Mbuffer containing the SDP
 *
 * @return 0 if success, otherwise errorcode
 */
int sdp_decode_multipart(const struct pl *ctype_prm, struct mbuf *mb)
{
	struct pl bnd, s, e, p;
	char expr[64];
	int err;

	if (!ctype_prm || !mb)
		return EINVAL;

	/* fetch the boundary tag, excluding quotes */
	err = re_regex(ctype_prm->p, ctype_prm->l,
		       "boundary=[~]+", &bnd);
	if (err)
		return err;

	if (re_snprintf(expr, sizeof(expr), "--%r[^]+", &bnd) < 0)
		return ENOMEM;

	/* find 1st boundary */
	err = re_regex((char *)mbuf_buf(mb), mbuf_get_left(mb), expr, &s);
	if (err)
		return err;

	/* iterate over each part */
	while (s.l > 2) {
		if (re_regex(s.p, s.l, expr, &e))
			return 0;

		p.p = s.p + 2;
		p.l = e.p - p.p - bnd.l - 2;

		/* valid part in "p" */
		decode_part(&p, mb);

		s = e;
	}

	return 0;
}
