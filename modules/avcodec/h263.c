/**
 * @file h263.c  H.263 video codec (RFC 4629)
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <libavcodec/avcodec.h>
#include "h26x.h"
#include "avcodec.h"


int h263_hdr_encode(const struct h263_hdr *hdr, struct mbuf *mb)
{
	uint32_t v; /* host byte order */

	v  = hdr->f<<31 | hdr->p<<30 | hdr->sbit<<27 | hdr->ebit<<24;
	v |= hdr->src<<21 | hdr->i<<20 | hdr->u<<19 | hdr->s<<18 | hdr->a<<17;
	v |= hdr->r<<13 | hdr->dbq<<11 | hdr->trb<<8 | hdr->tr<<0;

	return mbuf_write_u32(mb, htonl(v));
}


enum h263_mode h263_hdr_mode(const struct h263_hdr *hdr)
{
	if (!hdr->f) {
		return H263_MODE_A;
	}
	else {
		if (!hdr->p)
			return H263_MODE_B;
		else
			return H263_MODE_C;
	}
}


int h263_hdr_decode(struct h263_hdr *hdr, struct mbuf *mb)
{
	uint32_t v;

	if (!hdr)
		return EINVAL;
	if (mbuf_get_left(mb) < H263_HDR_SIZE_MODEA)
		return EBADMSG;

	v = ntohl(mbuf_read_u32(mb));

	/* Common */
	hdr->f    = v>>31 & 0x1;
	hdr->p    = v>>30 & 0x1;
	hdr->sbit = v>>27 & 0x7;
	hdr->ebit = v>>24 & 0x7;
	hdr->src  = v>>21 & 0x7;

	switch (h263_hdr_mode(hdr)) {

	case H263_MODE_A:
		hdr->i    = v>>20 & 0x1;
		hdr->u    = v>>19 & 0x1;
		hdr->s    = v>>18 & 0x1;
		hdr->a    = v>>17 & 0x1;
		hdr->r    = v>>13 & 0xf;
		hdr->dbq  = v>>11 & 0x3;
		hdr->trb  = v>>8  & 0x7;
		hdr->tr   = v>>0  & 0xff;
		break;

	case H263_MODE_B:
		hdr->quant = v>>16 & 0x1f;
		hdr->gobn  = v>>11 & 0x1f;
		hdr->mba   = v>>2  & 0x1ff;

		if (mbuf_get_left(mb) < 4)
			return EBADMSG;

		v = ntohl(mbuf_read_u32(mb));

		hdr->i     = v>>31 & 0x1;
		hdr->u     = v>>30 & 0x1;
		hdr->s     = v>>29 & 0x1;
		hdr->a     = v>>28 & 0x1;
		hdr->hmv1  = v>>21 & 0x7f;
		hdr->vmv1  = v>>14 & 0x7f;
		hdr->hmv2  = v>>7  & 0x7f;
		hdr->vmv2  = v>>0  & 0x7f;
		break;

	case H263_MODE_C:
		/* NOTE: Mode C is optional, only parts decoded */
		if (mbuf_get_left(mb) < 8)
			return EBADMSG;

		v = ntohl(mbuf_read_u32(mb));
		hdr->i    = v>>31 & 0x1;
		hdr->u    = v>>30 & 0x1;
		hdr->s    = v>>29 & 0x1;
		hdr->a    = v>>28 & 0x1;

		(void)mbuf_read_u32(mb); /* ignore */
		break;
	}

	return 0;
}


/**
 * Find PSC (Picture Start Code) in bit-stream
 *
 * @param p     Input bit-stream
 * @param size  Number of bytes in bit-stream
 *
 * @return Pointer to PSC if found, otherwise NULL
 */
const uint8_t *h263_strm_find_psc(const uint8_t *p, uint32_t size)
{
	const uint8_t *end = p + size - 1;

	for (; p < end; p++) {
		if (p[0] == 0x00 && p[1] == 0x00)
			return p;
	}

	return NULL;
}


int h263_strm_decode(struct h263_strm *s, struct mbuf *mb)
{
	const uint8_t *p;

	if (mbuf_get_left(mb) < 6)
		return EINVAL;

	p = mbuf_buf(mb);

	s->psc[0] = p[0];
	s->psc[1] = p[1];

	s->temp_ref = (p[2]<<6 & 0xc0) | (p[3]>>2 & 0x3f);

	s->split_scr        = p[4]>>7 & 0x1;
	s->doc_camera       = p[4]>>6 & 0x1;
	s->pic_frz_rel      = p[4]>>5 & 0x1;
	s->src_fmt          = p[4]>>2 & 0x7;
	s->pic_type         = p[4]>>1 & 0x1;
	s->umv              = p[4]>>0 & 0x1;

	s->sac              = p[5]>>7 & 0x1;
	s->apm              = p[5]>>6 & 0x1;
	s->pb               = p[5]>>5 & 0x1;
	s->pquant           = p[5]>>0 & 0x1f;

	s->cpm              = p[6]>>7 & 0x1;
	s->pei              = p[6]>>6 & 0x1;

	return 0;
}


/**
 * Copy H.263 bit-stream to H.263 RTP payload header
 *
 * @param hdr H.263 header to be written to
 * @param s   H.263 stream header
 */
void h263_hdr_copy_strm(struct h263_hdr *hdr, const struct h263_strm *s)
{
	hdr->f    = 0;  /* Mode A */
	hdr->p    = 0;
	hdr->sbit = 0;
	hdr->ebit = 0;
	hdr->src  = s->src_fmt;
	hdr->i    = s->pic_type;
	hdr->u    = s->umv;
	hdr->s    = s->sac;
	hdr->a    = s->apm;
	hdr->r    = 0;
	hdr->dbq  = 0;   /* No PB-frames */
	hdr->trb  = 0;   /* No PB-frames */
	hdr->tr   = s->temp_ref;
}
