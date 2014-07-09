/**
 * @file dtls_srtp/srtp.c Secure RTP
 *
 * Copyright (C) 2010 Creytiv.com
 */

#include <re.h>
#include <baresip.h>
#include "dtls_srtp.h"


struct srtp_stream {
	struct srtp *srtp;
};


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
static inline bool is_rtp_or_rtcp(const struct mbuf *mb)
{
	uint8_t b;

	if (mbuf_get_left(mb) < 1)
		return false;

	b = mbuf_buf(mb)[0];

	return 127 < b && b < 192;
}


static inline bool is_rtcp_packet(const struct mbuf *mb)
{
	uint8_t pt;

	if (mbuf_get_left(mb) < 2)
		return false;

	pt = mbuf_buf(mb)[1] & 0x7f;

	return 64 <= pt && pt <= 95;
}


static void destructor(void *arg)
{
	struct srtp_stream *s = arg;

	mem_deref(s->srtp);
}


static bool send_handler(int *err, struct sa *dst, struct mbuf *mb, void *arg)
{
	struct comp *comp = arg;
	(void)dst;

	if (!is_rtp_or_rtcp(mb))
		return false;

	if (is_rtcp_packet(mb)) {
		*err = srtcp_encrypt(comp->tx->srtp, mb);
		if (*err) {
			warning("srtp: srtcp_encrypt failed (%m)\n", *err);
		}
	}
	else {
		*err = srtp_encrypt(comp->tx->srtp, mb);
		if (*err) {
			warning("srtp: srtp_encrypt failed (%m)\n", *err);
		}
	}


	return *err ? true : false;  /* continue processing */
}


static bool recv_handler(struct sa *src, struct mbuf *mb, void *arg)
{
	struct comp *comp = arg;
	int err;
	(void)src;

	if (!is_rtp_or_rtcp(mb))
		return false;

	if (is_rtcp_packet(mb)) {
		err = srtcp_decrypt(comp->rx->srtp, mb);
	}
	else {
		err = srtp_decrypt(comp->rx->srtp, mb);
	}

	if (err) {
		warning("srtp: recv: failed to decrypt %s-packet (%m)\n",
			is_rtcp_packet(mb) ? "RTCP" : "RTP", err);
		return true;   /* error - drop packet */
	}

	return false;  /* continue processing */
}


int srtp_stream_add(struct srtp_stream **sp, enum srtp_suite suite,
		    const uint8_t *key, size_t key_size, bool tx)
{
	struct srtp_stream *s;
	int err = 0;
	(void)tx;

	if (!sp || !key)
		return EINVAL;

	s = mem_zalloc(sizeof(*s), destructor);
	if (!s)
		return ENOMEM;

	err = srtp_alloc(&s->srtp, suite, key, key_size, 0);
	if (err) {
		warning("srtp: srtp_alloc() failed (%m)\n", err);
		goto out;
	}

 out:
	if (err)
		mem_deref(s);
	else
		*sp = s;

	return err;
}


int srtp_install(struct comp *comp)
{
	return udp_register_helper(&comp->uh_srtp, comp->app_sock,
				   LAYER_SRTP,
				   send_handler, recv_handler, comp);
}
