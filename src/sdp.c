/**
 * @file src/sdp.c  SDP functions
 *
 * Copyright (C) 2011 Creytiv.com
 */
#include <stdlib.h>
#include <re.h>
#include <baresip.h>
#include "core.h"


uint32_t sdp_media_rattr_u32(const struct sdp_media *m, const char *name)
{
	const char *attr = sdp_media_rattr(m, name);
	return attr ? atoi(attr) : 0;
}


/*
 * Get a remote attribute from the SDP. Try the media-level first,
 * and if it does not exist then try session-level.
 */
const char *sdp_rattr(const struct sdp_session *s, const struct sdp_media *m,
		      const char *name)
{
	const char *x;

	x = sdp_media_rattr(m, name);
	if (x)
		return x;

	x = sdp_session_rattr(s, name);
	if (x)
		return x;

	return NULL;
}


/* RFC 4572 */
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


bool sdp_media_has_media(const struct sdp_media *m)
{
	bool has;

	has = sdp_media_rformat(m, NULL) != NULL;
	if (has)
		return sdp_media_rport(m) != 0;

	return false;
}


/**
 * Find a dynamic payload type that is not used
 *
 * @param m SDP Media
 *
 * @return Unused payload type, -1 if no found
 */
int sdp_media_find_unused_pt(const struct sdp_media *m)
{
	int pt;

	for (pt = PT_DYN_MAX; pt>=PT_DYN_MIN; pt--) {

		if (!sdp_media_format(m, false, NULL, pt, NULL, -1, -1))
			return pt;
	}

	return -1;
}


const struct sdp_format *sdp_media_format_cycle(struct sdp_media *m)
{
	struct sdp_format *sf;
	struct list *lst;

 again:
	sf = (struct sdp_format *)sdp_media_rformat(m, NULL);
	if (!sf)
		return NULL;

	lst = sf->le.list;

	/* move top-most codec to end of list */
	list_unlink(&sf->le);
	list_append(lst, &sf->le, sf);

	sf = (struct sdp_format *)sdp_media_rformat(m, NULL);
	if (!str_casecmp(sf->name, telev_rtpfmt))
		goto again;

	return sf;
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
 */
int sdp_decode_multipart(const struct pl *ctype, struct mbuf *mb)
{
	struct pl bnd, s, e, p;
	char expr[64];
	int err;

	if (!ctype || !mb)
		return EINVAL;

	/* fetch the boundary tag, excluding quotes */
	err = re_regex(ctype->p, ctype->l,
		       "multipart/mixed;[ \t]*boundary=[~]+", NULL, &bnd);
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
