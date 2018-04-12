/**
 * @file src/h264.c  H.264 video codec packetization (RFC 3984)
 *
 * Copyright (C) 2010 - 2015 Creytiv.com
 */
#include <string.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>


int h264_hdr_encode(const struct h264_hdr *hdr, struct mbuf *mb)
{
	uint8_t v;

	v = hdr->f<<7 | hdr->nri<<5 | hdr->type<<0;

	return mbuf_write_u8(mb, v);
}


int h264_hdr_decode(struct h264_hdr *hdr, struct mbuf *mb)
{
	uint8_t v;

	if (mbuf_get_left(mb) < 1)
		return ENOENT;

	v = mbuf_read_u8(mb);

	hdr->f    = v>>7 & 0x1;
	hdr->nri  = v>>5 & 0x3;
	hdr->type = v>>0 & 0x1f;

	return 0;
}


int h264_fu_hdr_encode(const struct h264_fu *fu, struct mbuf *mb)
{
	uint8_t v = fu->s<<7 | fu->s<<6 | fu->r<<5 | fu->type;
	return mbuf_write_u8(mb, v);
}


int h264_fu_hdr_decode(struct h264_fu *fu, struct mbuf *mb)
{
	uint8_t v;

	if (mbuf_get_left(mb) < 1)
		return ENOENT;

	v = mbuf_read_u8(mb);

	fu->s    = v>>7 & 0x1;
	fu->e    = v>>6 & 0x1;
	fu->r    = v>>5 & 0x1;
	fu->type = v>>0 & 0x1f;

	return 0;
}


/*
 * Find the NAL start sequence in a H.264 byte stream
 *
 * @note: copied from ffmpeg source
 */
const uint8_t *h264_find_startcode(const uint8_t *p, const uint8_t *end)
{
	const uint8_t *a = p + 4 - ((long)p & 3);

	for (end -= 3; p < a && p < end; p++ ) {
		if (p[0] == 0 && p[1] == 0 && p[2] == 1)
			return p;
	}

	for (end -= 3; p < end; p += 4) {
		uint32_t x = *(const uint32_t*)(void *)p;
		if ( (x - 0x01010101) & (~x) & 0x80808080 ) {
			if (p[1] == 0 ) {
				if ( p[0] == 0 && p[2] == 1 )
					return p;
				if ( p[2] == 0 && p[3] == 1 )
					return p+1;
			}
			if ( p[3] == 0 ) {
				if ( p[2] == 0 && p[4] == 1 )
					return p+2;
				if ( p[4] == 0 && p[5] == 1 )
					return p+3;
			}
		}
	}

	for (end += 3; p < end; p++) {
		if (p[0] == 0 && p[1] == 0 && p[2] == 1)
			return p;
	}

	return end + 3;
}


static int rtp_send_data(const uint8_t *hdr, size_t hdr_sz,
			 const uint8_t *buf, size_t sz,
			 bool eof, uint64_t rtp_ts,
			 videnc_packet_h *pkth, void *arg)
{
	return pkth(eof, rtp_ts, hdr, hdr_sz, buf, sz, arg);
}


int h264_nal_send(bool first, bool last,
		  bool marker, uint32_t ihdr, uint64_t rtp_ts,
		  const uint8_t *buf, size_t size, size_t maxsz,
		  videnc_packet_h *pkth, void *arg)
{
	uint8_t hdr = (uint8_t)ihdr;
	int err = 0;

	if (first && last && size <= maxsz) {
		err = rtp_send_data(&hdr, 1, buf, size, marker, rtp_ts,
				    pkth, arg);
	}
	else {
		uint8_t fu_hdr[2];
		const uint8_t type = hdr & 0x1f;
		const uint8_t nri  = hdr & 0x60;
		const size_t sz = maxsz - 2;

		fu_hdr[0] = nri | H264_NAL_FU_A;
		fu_hdr[1] = first ? (1<<7 | type) : type;

		while (size > sz) {
			err |= rtp_send_data(fu_hdr, 2, buf, sz, false,
					     rtp_ts,
					     pkth, arg);
			buf += sz;
			size -= sz;
			fu_hdr[1] &= ~(1 << 7);
		}

		if (last)
			fu_hdr[1] |= 1<<6;  /* end bit */

		err |= rtp_send_data(fu_hdr, 2, buf, size, marker && last,
				     rtp_ts,
				     pkth, arg);
	}

	return err;
}


int h264_packetize(uint64_t rtp_ts, const uint8_t *buf, size_t len,
		   size_t pktsize, videnc_packet_h *pkth, void *arg)
{
	const uint8_t *start = buf;
	const uint8_t *end   = buf + len;
	const uint8_t *r;
	int err = 0;

	r = h264_find_startcode(start, end);

	while (r < end) {
		const uint8_t *r1;

		/* skip zeros */
		while (!*(r++))
			;

		r1 = h264_find_startcode(r, end);

		err |= h264_nal_send(true, true, (r1 >= end), r[0],
				     rtp_ts, r+1, r1-r-1, pktsize,
				     pkth, arg);
		r = r1;
	}

	return err;
}


/**
 * Get the name of an H.264 nal unit
 *
 * @param type NAL unit type
 *
 * @return A string containing the NAL unit name
 */
const char *h264_nalunit_name(int type)
{
	switch (type) {

	case H264_NAL_SLICE:       return "SLICE";
	case H264_NAL_DPA:         return "DPA";
	case H264_NAL_DPB:         return "DPB";
	case H264_NAL_DPC:         return "DPC";
	case H264_NAL_IDR_SLICE:   return "IDR_SLICE";
	case H264_NAL_SEI:         return "SEI";
	case H264_NAL_SPS:         return "SPS";
	case H264_NAL_PPS:         return "PPS";
	case H264_NAL_AUD:         return "AUD";
	case H264_NAL_FILLER_DATA: return "FILLER";

	case H264_NAL_FU_A:        return "FU-A";
	case H264_NAL_FU_B:        return "FU-B";
	}

	return "???";
}
