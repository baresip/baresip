/**
 * @file av1/obu.c AV1 Open Bitstream Unit (OBU)
 *
 * Copyright (C) 2010 - 2021 Alfred E. Heggestad
 */

#include <string.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <aom/aom.h>
#include "av1.h"


int av1_leb128_encode(struct mbuf *mb, size_t value)
{
	size_t start;
	size_t value_copy = value;
	int err = 0;

	if (!mb)
		return EINVAL;

	start = mb->pos;

	while (value >= 0x80) {

		uint8_t u8 = 0x80 | (value & 0x7F);

		err |= mbuf_write_u8(mb, u8);

		value >>= 7;
	}

	/* Last byte will have MSB=0 */
	err |= mbuf_write_u8(mb, value);

	info(".... LEB encode [%zu] -> [ 0x%w ]\n",
	     value_copy, &mb->buf[start], mb->pos - start);

	return err;
}


size_t av1_leb128_decode(struct mbuf *mb)
{
	size_t ret = 0;
	size_t start = mb->pos;
	int i;

	if (!mb)
		return 0;

	for (i = 0; i < 8; i++) {

		size_t byte = mbuf_read_u8(mb);

		ret |= (size_t)(byte & 0x7f) << (i * 7);

		if (!(byte & 0x80))
			break;
	}

	info(".... LEB decode [%zu] <- [ 0x%w ]\n",
	     ret, &mb->buf[start], mb->pos - start);

	return ret;
}


int av1_obu_encode(struct mbuf *mb, unsigned type, bool has_size,
		   size_t len, const uint8_t *payload)
{
	uint8_t val;
	int err;

	if (!mb || type==0)
		return EINVAL;

#if 0
	info(".... av1: obu: encode:  type=%-24s  len=%zu\n",
	     aom_obu_type_to_string(type), len);
#endif

	val  = (type&0xf) << 3;
	val |= (unsigned)has_size << 1;

	err  = mbuf_write_u8(mb, val);

	if (has_size)
		err |= av1_leb128_encode(mb, len);

	if (payload && len)
		err |= mbuf_write_mem(mb, payload, len);

	return err;
}


int av1_obu_decode(struct obu_hdr *hdr, struct mbuf *mb)
{
	uint8_t val;

	if (!hdr || !mb)
		return EINVAL;

	if (mbuf_get_left(mb) < 1)
		return EBADMSG;

	memset(hdr, 0, sizeof(*hdr));

	val = mbuf_read_u8(mb);

	hdr->f    = (val >> 7) & 0x1;
	hdr->type = (val >> 3) & 0xf;
	hdr->x    = (val >> 2) & 0x1;
	hdr->s    = (val >> 1) & 0x1;

	if (hdr->f) {
		warning("av1: header: obu forbidden bit!"
			" [type=%s, x=%d, s=%d]\n",
			aom_obu_type_to_string(hdr->type),
			hdr->x, hdr->s);
		return EBADMSG;
	}

	if (hdr->type == 0) {
		warning("av1: header: obu type 0 is reserved\n");
		return EBADMSG;
	}

	if (hdr->x) {
		warning("av1: header: extension not supported (%s)\n",
			aom_obu_type_to_string(hdr->type));
		return ENOTSUP;
	}

	if (hdr->s) {
		hdr->size = av1_leb128_decode(mb);

		if (hdr->size > mbuf_get_left(mb)) {
			warning("short packet: %zu > %zu\n",
				hdr->size, mbuf_get_left(mb));
			return EBADMSG;
		}
	}
	else {
		hdr->size = mbuf_get_left(mb);
	}

	return 0;
}
