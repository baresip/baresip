/**
 * @file sender.c
 *
 * Copyright (C) 2021 Commend.com - c.huber@commend.com
 */

#include <re.h>
#include <rem.h>
#include <baresip.h>

#include <stdlib.h>
#ifdef HAVE_PTHREAD
#include <pthread.h>
#endif

#include "multicast.h"

#define DEBUG_MODULE "mcsend"
#define DEBUG_LEVEL 5
#include <re_dbg.h>


static struct list mcsenderl = LIST_INIT;


struct mcsender {
	struct le le;

	struct sa addr;
	struct rtp_sock *rtp;

	struct config_audio *cfg;
	const struct aucodec *ac;

	struct mcsource *src;
};


/**
 * @brief Multicasta sender destructor
 *
 * @param arg Multicast sender object
 */
static void mcsender_destructor(void *arg)
{
	struct mcsender *mcsender = arg;

	mcsender->src = mem_deref(mcsender->src);
	mcsender->rtp = mem_deref(mcsender->rtp);
}


/**
 * @brief Multicast addess comparison
 *
 * @param le	List element (mcsender)
 * @param arg	Argument     (address)
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
 * @brief Multicast send handler
 *
 * @param ext_len	RTP extension header Length
 * @param marker 	RTP marker
 * @param mb		Data to send
 *
 * @return int 0 if success, errorcode otherwise
 */
static int mcsender_send_handler(uint32_t ext_len, bool marker,
	uint32_t rtp_ts, struct mbuf *mb, void *arg)
{
	struct mcsender *mcsender = arg;
	struct pl placpt = PL_INIT;
	int err = 0;

	if (!mb)
		return EINVAL;

	pl_set_str(&placpt, mcsender->ac->pt);
	err = rtp_send(mcsender->rtp, &mcsender->addr, ext_len != 0, marker,
		pl_u32(&placpt), rtp_ts, mb);

	return err;
}


/**
 * @brief Stop all existing multicast sender
 */
void mcsender_stopall(void)
{
	list_flush(&mcsenderl);
}


/**
 * @brief Stop the multicast sender with @addr
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
 * @brief Allocate a new multicast sender object
 *
 * @param addr	Destination address
 * @param codec	Used audio codec
 *
 * @return int 0 if success, errorcode otherwise
 */
int mcsender_alloc(struct sa *addr, const struct aucodec *codec)
{
	int err = 0;
	struct mcsender *mcsender = NULL;

	if (!addr || !codec)
		return EINVAL;

	if (list_apply(&mcsenderl, true, mcsender_addr_cmp, addr))
		return EADDRINUSE;


	mcsender = mem_zalloc(sizeof(*mcsender), mcsender_destructor);
	if (!mcsender)
		return ENOMEM;

	sa_cpy(&mcsender->addr, addr);
	mcsender->ac = codec;

	err = rtp_open(&mcsender->rtp, sa_af(&mcsender->addr));
	if (err)
		goto out;

	err = mcsource_start(&mcsender->src, mcsender->ac,
		mcsender_send_handler, mcsender);

	list_append(&mcsenderl, &mcsender->le, mcsender);

 out:
	if (err)
		mem_deref(mcsender);

	return err;
}


/**
 * @brief Print all available multicast sender
 *
 * @param pf Printer
 */
void mcsender_print(struct re_printf *pf)
{
	struct le *le;
	struct mcsender *mcsender;

	re_hprintf(pf, "Multicast Sender List:\n");
	LIST_FOREACH(&mcsenderl, le) {
		mcsender = le->data;
		re_hprintf(pf, "   %J - %s\n", &mcsender->addr,
			mcsender->ac->name);
	}
}
