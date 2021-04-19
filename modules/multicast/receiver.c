/**
 * @file receiver.c
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

#define DEBUG_MODULE "mcreceiver"
#define DEBUG_LEVEL 6
#include <re_dbg.h>


struct list mcreceivl = LIST_INIT;
struct lock *mcreceivl_lock = NULL;


enum {
	TIMEOUT = 500,
};

/**
 * Multicast receiver struct
 *
 * Contains data to collect and controll all listeners
 */
struct mcreceiver {
	struct le le;
	struct sa addr;
	uint8_t prio;

	struct rtp_sock *rtp;
	uint32_t ssrc;
	struct jbuf *jbuf;

	const struct aucodec *ac;

	struct tmr timeout;

	bool running;
	bool enable;
	bool globenable;
};


static void mcreceiver_destructor(void *arg)
{
	struct mcreceiver *mcreceiver = arg;

	tmr_cancel(&mcreceiver->timeout);

	if (mcreceiver->running)
		mcplayer_stop();

	mcreceiver->ssrc = 0;
	mcreceiver->running = false;

	mcreceiver->rtp  = mem_deref(mcreceiver->rtp);
	mcreceiver->jbuf = mem_deref(mcreceiver->jbuf);

}


/**
 * Multicast address comparison
 *
 * @param le  List element (mcreceiver)
 * @param arg Argument     (address)
 *
 * @return true  if mcreceiver->addr == address
 * @return false if mcreceiver->addr != address
 */
static bool mcreceiver_addr_cmp(struct le *le, void *arg)
{
	struct mcreceiver *mcreceiver = le->data;
	struct sa *addr = arg;

	return sa_cmp(&mcreceiver->addr, addr, SA_ALL);
}


/**
 * Multicast receiver priority comparison
 *
 * @param le  List element (mcreceiver)
 * @param arg Argument     (priority)
 *
 * @return true if mcreceiver->prio == prio
 * @return false if mcreceiver->prio != prio
 */
static bool mcreceiver_prio_cmp(struct le *le, void *arg)
{
	struct mcreceiver *mcreceiver = le->data;
	uint32_t *prio = (uint32_t *)arg;

	return mcreceiver->prio == *prio;
}


/**
 * Get running multicast receiver
 *
 * @param le  Multicast receiver list element
 * @param arg Unused
 *
 * @return true if multicast receiver is running
 * @return false if multicast receiver is not running
 */
static bool mcreceiver_running(struct le *le, void *arg)
{
	struct mcreceiver *mcreceiver = le->data;
	(void) arg;

	return mcreceiver->running;
}


/**
 * Convert std rtp codec payload type to audio codec
 *
 * @param hdr RTP header object
 *
 * @return struct aucodec*
 */
static const struct aucodec *pt2codec(const struct rtp_header *hdr)
{
	const struct aucodec *codec = NULL;

	switch (hdr->pt) {
		case 0:
			codec = aucodec_find(baresip_aucodecl(), "PCMU", 0, 1);
			break;

		case 8:
			codec = aucodec_find(baresip_aucodecl(), "PCMA", 0, 1);
			break;

		case 9:
			codec = aucodec_find(baresip_aucodecl(), "G722", 0, 1);
			break;

		default:
			warning ("multicast receiver: RTP Payload "
				"Type %d not found.\n");
			break;
	}

	return codec;
}


/**
 * Resume to the pre-multicast uag state if no other high priority
 * multicasts are running
 */
static void resume_uag_state(void)
{
	uint8_t h = 255;
	struct le *le= NULL;
	struct mcreceiver *mcreceiver = NULL;

	for (le = list_head(&mcreceivl); le; le = le->next) {
		mcreceiver = le->data;

		if (mcreceiver->ssrc && mcreceiver->prio < h)
			h = mcreceiver->prio;
	}

	if (h > multicast_callprio()) {
		uag_set_dnd(false);
		uag_set_nodial(false);
		uag_hold_resume(NULL);
	}
}


/**
 * Multicast Priority handling
 *
 * @param mcreceiver Multicast receiver object
 * @param ssrc       SSRC of received RTP packet
 *
 * @return int 0 if success, errorcode otherwise
 */
static int prio_handling(struct mcreceiver *mcreceiver, uint32_t ssrc)
{
	int err = 0;
	struct le *le;
	struct mcreceiver *hprio = NULL;

	if (!mcreceiver)
		return EINVAL;

	err = lock_write_try(mcreceivl_lock);
	if (err)
		return err;

	if (mcreceiver->prio < multicast_callprio()) {
		uag_set_dnd(true);
		uag_set_nodial(true);
	}

	le = list_apply(&mcreceivl, true, mcreceiver_running, NULL);
	if (!le) {
		/* start the player now */
		mcplayer_stop();
		jbuf_flush(mcreceiver->jbuf);
		mcreceiver->running = true;
		mcreceiver->ssrc = ssrc;
		ua_event(NULL, UA_EVENT_CUSTOM, NULL,
			"multicast: receive start %J (%d)", &mcreceiver->addr,
			mcreceiver->prio);
		err = mcplayer_start(mcreceiver->jbuf, mcreceiver->ac);
		goto out;
	}

	hprio = le->data;
	if (hprio->prio < mcreceiver->prio)
		/*received lower prio -> noting todo*/
		goto out;

	if (hprio->prio == mcreceiver->prio && mcreceiver->ssrc != ssrc) {
		/*SSRC changed -> restart player*/
		mcplayer_stop();
		jbuf_flush(hprio->jbuf);
		hprio->ssrc = ssrc;
		ua_event(NULL, UA_EVENT_CUSTOM, NULL,
			"multicast: receive start %J (%d)", &hprio->addr,
			hprio->prio);
		err = mcplayer_start(hprio->jbuf, hprio->ac);
		goto out;
	}
	else if (hprio->prio == mcreceiver->prio) {
		/*same prio but no new stream -> nothing todo*/
		goto out;
	}

	/*higher prio -> stop old player and start new one*/
	mcplayer_stop();
	hprio->running = false;
	jbuf_flush(mcreceiver->jbuf);
	mcreceiver->ssrc = ssrc;
	mcreceiver->running = true;
	ua_event(NULL, UA_EVENT_CUSTOM, NULL,
		"multicast: receive start %J (%d)", &mcreceiver->addr,
		mcreceiver->prio);

	err = mcplayer_start(mcreceiver->jbuf, mcreceiver->ac);

  out:
	lock_rel(mcreceivl_lock);
	return err;
}


/**
 * RTP timeout handler
 *
 * @param arg Multicast receiver object
 */
static void timeout_handler(void *arg)
{
	struct mcreceiver *mcreceiver = arg;
	info ("multicast receiver: timeout of %J (prio=%d)\n",
		&mcreceiver->addr, mcreceiver->prio);

	lock_write_get(mcreceivl_lock);

	if (mcreceiver->running) {
		ua_event(NULL, UA_EVENT_CUSTOM, NULL,
			"multicast: receive timeout %J", &mcreceiver->addr);
		mcplayer_stop();
	}

	mcreceiver->running = false;
	mcreceiver->ssrc = 0;
	mcreceiver->ac = NULL;

	resume_uag_state();

	lock_rel(mcreceivl_lock);
	return;
}


/**
 * Handle incoming RTP packages
 *
 * @param src Source address
 * @param hdr RTP headers
 * @param mb  RTP payload
 * @param arg Multicast receiver object
 */
static void rtp_handler(const struct sa *src, const struct rtp_header *hdr,
	struct mbuf *mb, void *arg)
{
	int err = 0;
	struct mcreceiver *mcreceiver = arg;

	(void) src;
	(void) mb;

	if (!mcreceiver->enable)
		goto out;

	if (!mcreceiver->globenable)
		goto out;

	if (mcreceiver->prio >= multicast_callprio() && uag_call_count()) {
		goto out;
	}
	else if (mcreceiver->prio < multicast_callprio() && uag_call_count()) {
		struct le *leua;
		struct ua *ua;

		for (leua = list_head(uag_list()); leua; leua = leua->next) {
			struct le *le;
			ua = leua->data;
			for (le = list_head(ua_calls(ua)); le; le = le->next) {
				struct call *call = le->data;
				if (!call_is_onhold(call))
					call_hold(call, true);
			}
		}
	}

	mcreceiver->ac = pt2codec(hdr);
	if (!mcreceiver->ac)
		goto out;

	if (!mbuf_get_left(mb))
		goto out;

	err = prio_handling(mcreceiver, hdr->ssrc);
	if (err)
		goto out;

	mcreceiver->ssrc = hdr->ssrc;


	err = jbuf_put(mcreceiver->jbuf, hdr, mb);
	if (err)
		return;

  out:
	tmr_start(&mcreceiver->timeout, TIMEOUT, timeout_handler, mcreceiver);

	return;
}


/**
 * Enable / Disable all mcreceiver with prio > (argument)prio
 *
 * @param prio Priority
 */
void mcreceiver_enprio(uint32_t prio)
{
	struct le *le;
	struct mcreceiver *mcreceiver;

	if (!prio)
		return;

	lock_write_get(mcreceivl_lock);
	LIST_FOREACH(&mcreceivl, le) {
		mcreceiver = le->data;

		if (mcreceiver->prio <= prio)
			mcreceiver->enable = true;
		else
			mcreceiver->enable = false;
	}

	lock_rel(mcreceivl_lock);
}


/**
 * Enable / Disable all multicast receiver
 *
 * @param enable
 */
void mcreceiver_enable(bool enable)
{
	struct le *le;
	struct mcreceiver *mcreceiver;

	lock_write_get(mcreceivl_lock);
	LIST_FOREACH(&mcreceivl, le) {
		mcreceiver = le->data;
		mcreceiver->globenable = enable;
	}
	lock_rel(mcreceivl_lock);
}


/**
 * Change the priority of a multicast receiver
 *
 * @param addr Listen address
 * @param prio Priority
 *
 * @return int 0 if success, errorcode otherwise
 */
int mcreceiver_chprio(struct sa *addr, uint32_t prio)
{
	struct le *le;
	struct mcreceiver *mcreceiver;

	if (!addr || !prio)
		return EINVAL;

	le = list_apply(&mcreceivl, true, mcreceiver_addr_cmp, addr);
	if (!le) {
		warning ("multicast receiver: receiver %J not found\n", addr);
		return EINVAL;
	}

	if (list_apply(&mcreceivl, true, mcreceiver_prio_cmp, &prio)) {
		warning ("multicast receiver: priority %d already in use\n",
			prio);
		return EADDRINUSE;
	}

	mcreceiver = le->data;
	lock_write_get(mcreceivl_lock);
	mcreceiver->prio = prio;
	lock_rel(mcreceivl_lock);

	return 0;
}


/**
 * Un-register all multicast listener
 */
void mcreceiver_unregall(void)
{
	lock_write_get(mcreceivl_lock);
	list_flush(&mcreceivl);
	resume_uag_state();
	lock_rel(mcreceivl_lock);
	mcreceivl_lock = mem_deref(mcreceivl_lock);
}


/**
 * Un-register a multicast listener
 *
 * @param addr Listen address
 */
void mcreceiver_unreg(struct sa *addr){
	struct mcreceiver *mcreceiver = NULL;
	struct le *le;

	le = list_apply(&mcreceivl, true, mcreceiver_addr_cmp, addr);
	if (!le) {
		warning ("multicast: multicast receiver %J not found\n", addr);
		return;
	}

	mcreceiver = le->data;
	lock_write_get(mcreceivl_lock);
	list_unlink(&mcreceiver->le);
	resume_uag_state();
	lock_rel(mcreceivl_lock);
	mem_deref(mcreceiver);

	if (list_isempty(&mcreceivl))
		mcreceivl_lock = mem_deref(mcreceivl_lock);
}


/**
 * Allocate a new multicast receiver object
 *
 * @param addr Listen address
 * @param prio Listener priority
 *
 * @return int 0 if success, errorcode otherwise
 */
int mcreceiver_alloc(struct sa *addr, uint8_t prio)
{
	int err = 0;
	uint16_t port;
	struct mcreceiver *mcreceiver = NULL;
	struct config_avt *cfg = &conf_config()->avt;

	if (!addr || !prio)
		return EINVAL;

	if (list_apply(&mcreceivl, true, mcreceiver_addr_cmp, addr)) {
		warning ("multicast receiver: address %J already in use\n",
			addr);
		return EADDRINUSE;
	}

	if (list_apply(&mcreceivl, true, mcreceiver_prio_cmp, &prio)) {
		warning ("multicast receiver: priority %d already in use\n",
			prio);
		return EADDRINUSE;
	}

	mcreceiver = mem_zalloc(sizeof(*mcreceiver), mcreceiver_destructor);
	if (!mcreceiver)
		return ENOMEM;

	if (!mcreceivl_lock) {
		err = lock_alloc(&mcreceivl_lock);
		if (err)
			goto out;
	}

	sa_cpy(&mcreceiver->addr, addr);
	port = sa_port(&mcreceiver->addr);
	mcreceiver->prio = prio;

	mcreceiver->running = false;
	mcreceiver->enable = true;
	mcreceiver->globenable = true;

	err = jbuf_alloc(&mcreceiver->jbuf,
		cfg->jbuf_del.min, cfg->jbuf_del.max);
	err |= jbuf_set_type(mcreceiver->jbuf, cfg->jbtype);
	err |= jbuf_set_wish(mcreceiver->jbuf, cfg->jbuf_wish);
	if (err)
		goto out;


	err = rtp_listen(&mcreceiver->rtp, IPPROTO_UDP, &mcreceiver->addr,
		port, port + 1, false, rtp_handler, NULL, mcreceiver);
	if (err) {
		warning("multicast receiver: rtp listen failed:"
			"af=%s port=%u-&u (%m)\n", net_af2name(sa_af(addr)),
			port, port + 1, err);
		goto out;
	}

	lock_write_get(mcreceivl_lock);
	list_append(&mcreceivl, &mcreceiver->le, mcreceiver);
	lock_rel(mcreceivl_lock);

  out:
	if (err)
		mem_deref(mcreceiver);

	return err;
}


/**
 * Print all available multicast receiver
 *
 * @param pf Printer
 */
void mcreceiver_print(struct re_printf *pf)
{
	struct le *le = NULL;
	struct mcreceiver *mcreceiver = NULL;

	re_hprintf(pf, "Multicast Receiver List:\n");
	LIST_FOREACH(&mcreceivl, le) {
		mcreceiver = le->data;
		re_hprintf(pf, "   %J - %d%s%s\n", &mcreceiver->addr,
			mcreceiver->prio,
			mcreceiver->enable  && mcreceiver->globenable ?
			" (enable)" : "",
			mcreceiver->running ? " (active)" : "");
	}
}
