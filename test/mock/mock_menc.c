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


struct menc_sess {
	menc_event_h *eventh;
	void *arg;
};


struct menc_media {
	struct udp_sock *rtpsock;
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


static void sess_destructor(void *arg)
{
	struct menc_sess *sess = arg;
	(void)sess;
}


static int mock_session_alloc(struct menc_sess **sessp,
			      struct sdp_session *sdp, bool offerer,
			      menc_event_h *eventh, menc_error_h *errorh,
			      void *arg)
{
	struct menc_sess *sess;
	(void)offerer;
	(void)errorh;

	if (!sessp || !sdp)
		return EINVAL;

	sess = mem_zalloc(sizeof(*sess), sess_destructor);
	if (!sess)
		return ENOMEM;

	sess->eventh  = eventh;
	sess->arg     = arg;

	*sessp = sess;

	return 0;
}


static int mock_media_alloc(struct menc_media **mmp, struct menc_sess *sess,
			   struct rtp_sock *rtp,
			   struct udp_sock *rtpsock, struct udp_sock *rtcpsock,
			   const struct sa *raddr_rtp,
			   const struct sa *raddr_rtcp,
			    struct sdp_media *sdpm,
			    const struct stream *strm)
{
	struct menc_media *mm;
	const int layer = 10; /* above zero */
	int err = 0;
	(void)sess;
	(void)rtp;
	(void)rtcpsock;
	(void)raddr_rtp;
	(void)raddr_rtcp;

	if (!mmp || !sdpm)
		return EINVAL;

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

	err = sdp_media_set_lattr(sdpm, true, "xrtp", NULL);
	if (err)
		goto out;

	if (sdp_media_rattr(sdpm, "xrtp")) {

		char buf[64];

		re_snprintf(buf, sizeof(buf), "%s,xrtp",
			    sdp_media_name(sdpm));

		if (sess->eventh)
			sess->eventh(MENC_EVENT_SECURE, buf,
				     (struct stream *)strm, sess->arg);
	}

 out:
	if (err)
		mem_deref(mm);

	return err;
}


static struct menc menc_mock = {
	.id        = "XRTP",
	.sdp_proto = "RTP/XAVP",
	.wait_secure = true,
	.sessh     = mock_session_alloc,
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
