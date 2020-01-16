/**
 * @file rtpext.c  RTP Header Extensions
 *
 * Copyright (C) 2017 Creytiv.com
 */

#include <string.h>
#include <re.h>
#include <baresip.h>
#include "core.h"


/*
 * RFC 5285 A General Mechanism for RTP Header Extensions
 *
 * - One-Byte Header:  Supported
 * - Two-Byte Header:  Not supported
 */


int rtpext_hdr_encode(struct mbuf *mb, size_t num_bytes)
{
	int err = 0;

	if (!mb || !num_bytes)
		return EINVAL;

	if (num_bytes & 0x3) {
		warning("rtpext: hdr_encode: num_bytes (%zu) must be multiple"
			" of 4\n", num_bytes);
		return EINVAL;
	}

	err |= mbuf_write_u16(mb, htons(RTPEXT_TYPE_MAGIC));
	err |= mbuf_write_u16(mb, htons((uint16_t)(num_bytes / 4)));

	return err;
}


int rtpext_encode(struct mbuf *mb, unsigned id, unsigned len,
		  const uint8_t *data)
{
	size_t start;
	int err;

	if (!mb || !data)
		return EINVAL;

	if (id < RTPEXT_ID_MIN || id > RTPEXT_ID_MAX)
		return EINVAL;
	if (len < RTPEXT_LEN_MIN || len > RTPEXT_LEN_MAX)
		return EINVAL;

	start = mb->pos;

	err  = mbuf_write_u8(mb, id << 4 | (len-1));
	err |= mbuf_write_mem(mb, data, len);
	if (err)
		return err;

	/* padding */
	while ((mb->pos - start) & 0x03)
		err |= mbuf_write_u8(mb, 0x00);

	return err;
}


int rtpext_decode(struct rtpext *ext, struct mbuf *mb)
{
	uint8_t v;
	int err;

	if (!ext || !mb)
		return EINVAL;

	if (mbuf_get_left(mb) < 1)
		return EBADMSG;

	memset(ext, 0, sizeof(*ext));

	v = mbuf_read_u8(mb);

	ext->id  = v >> 4;
	ext->len = (v & 0x0f) + 1;

	if (ext->id < RTPEXT_ID_MIN || ext->id > RTPEXT_ID_MAX) {
		warning("rtpext: invalid ID %u\n", ext->id);
		return EBADMSG;
	}
	if (ext->len > mbuf_get_left(mb)) {
		warning("rtpext: short read\n");
		return ENODATA;
	}

	err = mbuf_read_mem(mb, ext->data, ext->len);
	if (err)
		return err;

	/* skip padding */
	while (mbuf_get_left(mb)) {
		uint8_t pad = mbuf_buf(mb)[0];

		if (pad != 0x00)
			break;

		mbuf_advance(mb, 1);
	}

	return 0;
}
