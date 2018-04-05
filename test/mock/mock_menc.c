/**
 * @file mock/mock_menc.c Mock media encryption
 *
 * Copyright (C) 2010 - 2018 Creytiv.com
 */

#include <string.h>
#include <re.h>
#include <baresip.h>
#include "../test.h"


#define SECRET_KEY 0xdd


struct menc_media {
	void *rtpsock;
	struct udp_helper *uh_rtp;
};


/*
 * Encrypt/decrypt an RTP payload with a dummy key.
 * We use a simple XOR scheme for simplicity.
 */
static void mock_crypt(struct mbuf *mb)
{
	size_t i, len = mbuf_get_left(mb);

	for (i = RTP_HEADER_SIZE; i < len; i++) {
		mb->buf[mb->pos + i] ^= SECRET_KEY;
	}
}


static void media_destructor(void *data)
{
	struct menc_media *mm = data;

	mem_deref(mm->uh_rtp);
	mem_deref(mm->rtpsock);
}


static bool send_handler(int *err, struct sa *dst, struct mbuf *mb, void *arg)
{
	struct menc_media *mm = arg;
	(void)mm;
	(void)err;
	(void)dst;

	mock_crypt(mb);

	return false;  /* continue processing */
}


static bool recv_handler(struct sa *src, struct mbuf *mb, void *arg)
{
	struct menc_media *mm = arg;
	(void)mm;
	(void)src;

	mock_crypt(mb);

	return false;  /* continue processing */
}


static int mock_media_alloc(struct menc_media **mmp, struct menc_sess *sess,
			    struct rtp_sock *rtp, int proto,
			    void *rtpsock, void *rtcpsock,
			    struct sdp_media *sdpm)
{
	struct menc_media *mm;
	const int layer = 10; /* above zero */
	int err = 0;
	(void)sess;
	(void)rtp;
	(void)rtcpsock;

	if (!mmp || !sdpm)
		return EINVAL;
	if (proto != IPPROTO_UDP)
		return EPROTONOSUPPORT;

	mm = *mmp;
	if (!mm) {
		mm = mem_zalloc(sizeof(*mm), media_destructor);
		if (!mm)
			return ENOMEM;

		mm->rtpsock = mem_ref(rtpsock);
		err = udp_register_helper(&mm->uh_rtp, rtpsock, layer,
					  send_handler, recv_handler, mm);
		if (err)
			goto out;

		*mmp = mm;
	}

 out:
	if (err)
		mem_deref(mm);

	return err;
}


static struct menc menc_mock = {
	.id        = "XRTP",
	.sdp_proto = "RTP/XAVP",
	.mediah    = mock_media_alloc
};


void mock_menc_register(void)
{
	menc_register(baresip_mencl(), &menc_mock);
}


void mock_menc_unregister(void)
{
	menc_unregister(&menc_mock);
}
