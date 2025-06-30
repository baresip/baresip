/**
 * @file test/jbuf_gnack.c Jitterbuffer GNACK Testcode
 *
 * Copyright (C) 2025 Sebastian Reimers
 */
#include <re.h>
#include <baresip.h>
#include "test.h"

struct agent {
	struct agent *peer;
	struct rtp_sock *rtp_sock;
	struct sa laddr_rtp;
	struct sa laddr_rtcp;
	struct jbuf *jb;
	int rtcp_rtpfb_count;
	int err;
};

struct test_rtp {
	uint16_t seq;
	uint32_t ts;
};

struct test_gnack {
	uint16_t pid;
	uint16_t blp;
};

/* Simulate single Video Frame 90000 Hz / 30 fps = 3000 with lost */
static const struct test_rtp testv_rtps[] = {
	{0, 3000},
	{1, 3000},
	{5, 3000},
	{10, 3000},
};

static const struct test_gnack testv_gnacks[] = {
	{2, 3},
	{6, 7},
};


static void rtp_recv_handler(const struct sa *src,
			     const struct rtp_header *hdr, struct mbuf *mb,
			     void *arg)
{
	struct agent *ag = arg;
	(void)src;

	if (!ag->jb)
		return;

	ag->err = jbuf_put(ag->jb, hdr, mb);
	if (ag->err)
		re_cancel();
}


static void rtcp_recv_handler(const struct sa *src, struct rtcp_msg *msg,
			      void *arg)
{
	struct agent *ag = arg;
	int err		 = 0;
	(void)src;

	switch (msg->hdr.pt) {

	case RTCP_RTPFB:
		ASSERT_EQ(testv_gnacks[ag->rtcp_rtpfb_count].pid,
			  msg->r.fb.fci.gnackv->pid);
		ASSERT_EQ(testv_gnacks[ag->rtcp_rtpfb_count].blp,
			  msg->r.fb.fci.gnackv->blp);
		++ag->rtcp_rtpfb_count;
		break;

	case RTCP_PSFB:
	case RTCP_APP:
	case RTCP_SR:
	case RTCP_RR:
	case RTCP_SDES:
		/* ignore */
		break;

	default:
		warning("unexpected RTCP message: %H\n", rtcp_msg_print, msg);
		err = EPROTO;
		break;
	}

	if (ag->rtcp_rtpfb_count >= (int)RE_ARRAY_SIZE(testv_gnacks))
		re_cancel();

out:
	if (err) {
		ag->err = err;
		re_cancel();
	}
}


static int agent_init(struct agent *ag)
{
	struct sa laddr;
	int err;

	sa_set_str(&laddr, "127.0.0.1", 0);

	err = rtp_listen(&ag->rtp_sock, IPPROTO_UDP, &laddr, 1024, 65535, true,
			 rtp_recv_handler, rtcp_recv_handler, ag);
	TEST_ERR(err);

	rtcp_set_srate_tx(ag->rtp_sock, 90000);
	rtcp_set_srate_rx(ag->rtp_sock, 90000);

	rtcp_enable_mux(ag->rtp_sock, true);

	udp_local_get(rtp_sock(ag->rtp_sock), &ag->laddr_rtp);
	udp_local_get(rtcp_sock(ag->rtp_sock), &ag->laddr_rtcp);

out:

	return err;
}


int test_jbuf_gnack(void)
{
	struct agent a = {.peer = NULL}, b = {.peer = NULL};
	struct mbuf *mb = NULL;
	int err;

	err = agent_init(&a);
	TEST_ERR(err);
	err = agent_init(&b);
	TEST_ERR(err);

	a.peer = &b;
	b.peer = &a;

	rtcp_start(a.rtp_sock, "cname", &b.laddr_rtcp);
	rtcp_start(b.rtp_sock, "cname", &a.laddr_rtcp);

	err = jbuf_alloc(&b.jb, 100, 100, 50);
	TEST_ERR(err);

	err = jbuf_set_type(b.jb, JBUF_FIXED);
	TEST_ERR(err);

	jbuf_set_srate(b.jb, 90000);

	jbuf_set_gnack(b.jb, b.rtp_sock);

	mb = mbuf_alloc(RTP_HEADER_SIZE + 1);
	if (!mb) {
		err = ENOMEM;
		goto out;
	}

	mbuf_fill(mb, 0x00, RTP_HEADER_SIZE + 1);

	/* Send some RTP-packets */
	for (size_t i = 0; i < RE_ARRAY_SIZE(testv_rtps); i++) {
		mb->pos = RTP_HEADER_SIZE;
		err = rtp_resend(a.rtp_sock, testv_rtps[i].seq, &b.laddr_rtp,
				 false, false, 0, testv_rtps[i].ts, mb);
		TEST_ERR(err);
	}

	err = re_main_timeout(500);
	TEST_ERR(err);

	err = a.err;
	TEST_ERR(err);
	err = b.err;
	TEST_ERR(err);

out:
	mem_deref(mb);
	mem_deref(a.rtp_sock);
	mem_deref(b.rtp_sock);
	mem_deref(b.jb);

	return err;
}
