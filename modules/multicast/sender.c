/**
 * @file sender.c
 *
 * Copyright (C) 2021 Commend.com - c.huber@commend.com
 */

#include <re.h>
#include <rem.h>
#include <baresip.h>

#include <stdlib.h>

#include "multicast.h"

#define DEBUG_MODULE "mcsend"
#define DEBUG_LEVEL 6
#include <re_dbg.h>


static struct list mcsenderl = LIST_INIT;


/**
 * Multicast sender struct
 *
 * Contains data to send audio stream to the network
 */
struct mcsender {
	struct le le;

	struct sa addr;
	struct rtp_sock *rtp;

	struct config_audio *cfg;
	const struct aucodec *ac;

	struct mcsource *src;
	bool enable;
};


static void mcsender_destructor(void *arg)
{
	struct mcsender *mcsender = arg;

	mcsource_stop(mcsender->src);
	mcsender->src = mem_deref(mcsender->src);
	mcsender->rtp = mem_deref(mcsender->rtp);
}


/**
 * Multicast address comparison
 *
 * @param le  List element (mcsender)
 * @param arg Argument     (address)
 *
 * @return true  if mcsender->addr == address
 * @return false if mcsender->addr != address
 */
static bool mcsender_addr_cmp(struct le *le, void *arg)
{
	struct mcsender *mcsender = le->data;
	struct sa *addr = arg;

	return sa_cmp(&mcsender->addr, addr, SA_ALL);
}


/**
 * Multicast send handler
 *
 * @param ext_len RTP extension header Length
 * @param marker  RTP marker
 * @param mb      Data to send
 *
 * @return 0 if success, otherwise errorcode
 */
static int mcsender_send_handler(size_t ext_len, bool marker,
	uint32_t rtp_ts, struct mbuf *mb, void *arg)
{
	struct mcsender *mcsender = arg;
	struct pl placpt = PL_INIT;
	int err = 0;

	if (!mb)
		return EINVAL;

	if (!mcsender->enable)
		return 0;

	if (uag_call_count())
		return 0;

	pl_set_str(&placpt, mcsender->ac->pt);
	err = rtp_send(mcsender->rtp, &mcsender->addr, ext_len != 0, marker,
		pl_u32(&placpt), rtp_ts, tmr_jiffies_rt_usec(), mb);

	return err;
}


/**
 * Enable / Disable all existing sender
 *
 * @param enable
 */
void mcsender_enable(bool enable)
{
	struct le *le;
	struct mcsender *mcsender;

	LIST_FOREACH(&mcsenderl, le) {
		mcsender = le->data;
		mcsender->enable = enable;
	}
}


/**
 * Stop all existing multicast sender
 */
void mcsender_stopall(void)
{
	list_flush(&mcsenderl);
}


/**
 * Stop the multicast sender with addr
 *
 * @param addr Address
 */
void mcsender_stop(struct sa *addr)
{
	struct mcsender *mcsender = NULL;
	struct le *le;

	le = list_apply(&mcsenderl, true, mcsender_addr_cmp, addr);
	if (!le) {
		warning ("multicast: multicast sender %J not found\n", addr);
		return;
	}

	mcsender = le->data;
	list_unlink(&mcsender->le);
	mem_deref(mcsender);
}


/**
 * Allocate a new multicast sender object
 *
 * @param addr  Destination address
 * @param codec Used audio codec
 *
 * @return 0 if success, otherwise errorcode
 */
int mcsender_alloc(struct sa *addr, const struct aucodec *codec)
{
	int err = 0;
	struct mcsender *mcsender = NULL;
	uint8_t ttl = multicast_ttl();

	if (!addr || !codec)
		return EINVAL;

	if (list_apply(&mcsenderl, true, mcsender_addr_cmp, addr))
		return EADDRINUSE;


	mcsender = mem_zalloc(sizeof(*mcsender), mcsender_destructor);
	if (!mcsender)
		return ENOMEM;

	sa_cpy(&mcsender->addr, addr);
	mcsender->ac = codec;
	mcsender->enable = true;

	err = rtp_open(&mcsender->rtp, sa_af(&mcsender->addr));
	if (err)
		goto out;

	if (ttl > 1) {
		struct udp_sock *sock;

		sock = (struct udp_sock *) rtp_sock(mcsender->rtp);
		udp_setsockopt(sock, IPPROTO_IP,
			IP_MULTICAST_TTL, &ttl, sizeof(ttl));
	}

	err = mcsource_start(&mcsender->src, mcsender->ac,
		mcsender_send_handler, mcsender);

	list_append(&mcsenderl, &mcsender->le, mcsender);

 out:
	if (err)
		mem_deref(mcsender);

	return err;
}


/**
 * Print all available multicast sender
 *
 * @param pf Printer
 */
void mcsender_print(struct re_printf *pf)
{
	struct le *le = NULL;
	struct mcsender *mcsender = NULL;

	re_hprintf(pf, "Multicast Sender List:\n");
	LIST_FOREACH(&mcsenderl, le) {
		mcsender = le->data;
		re_hprintf(pf, "   %J - %s%s\n", &mcsender->addr,
			mcsender->ac->name,
			mcsender->enable ? " (enabled)" : " (disabled)");
	}
}
