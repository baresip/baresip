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
	TIMEOUT = 1000,
};

enum state {
	LISTENING,
	RECEIVING,
	RUNNING,
	IGNORED,
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

	struct udp_sock *rtp;
	uint32_t ssrc;
	struct jbuf *jbuf;

	const struct aucodec *ac;

	struct tmr timeout;

	enum state state;
	bool enable;
};


static void resume_uag_state(void);


static char* state_str(enum state s) {
	switch (s) {
		case LISTENING:
			return "listening";
		case RECEIVING:
			return "receiving";
		case RUNNING:
			return "running";
		case IGNORED:
			return "ignored";
		default:
			return "???";
	}
}


static void mcreceiver_destructor(void *arg)
{
	struct mcreceiver *mcreceiver = arg;

	tmr_cancel(&mcreceiver->timeout);

	if (mcreceiver->state == RUNNING)
		mcplayer_stop();

	mcreceiver->ssrc = 0;

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

	return mcreceiver->state == RUNNING;
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
				"Type %d not found.\n", hdr->pt);
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

		if (mcreceiver->state == RUNNING && mcreceiver->prio < h)
			h = mcreceiver->prio;
	}

	if (h > multicast_callprio()) {
		uag_set_dnd(false);
		uag_set_nodial(false);
		uag_hold_resume(NULL);
	}
}


/**
 * Stops, flush, start player
 *
 * @param mcreceiver Multicast receiver object
 *
 * @return int 0 if success, errorcode otherwise
 */
static int player_stop_start(struct mcreceiver *mcreceiver)
{
	mcplayer_stop();
	jbuf_flush(mcreceiver->jbuf);
	return mcplayer_start(mcreceiver->jbuf, mcreceiver->ac);
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

	if (mcreceiver->state == LISTENING) {
		mcreceiver->state = RECEIVING;

		info ("multicast receiver: start addr=%J prio=%d enabled=%d "
			"state=%s\n", &mcreceiver->addr, mcreceiver->prio,
			mcreceiver->enable, state_str(mcreceiver->state));

		module_event("multicast", "receiver start", NULL, NULL,
			"addr=%J prio=%d enabled=%d state=%s",
			&mcreceiver->addr, mcreceiver->prio,
			mcreceiver->enable, state_str(mcreceiver->state));

	}

	if (!mcreceiver->enable) {
		mcreceiver->state = RECEIVING;
		err = ECANCELED;
		goto out;
	}

	if (mcreceiver->state == IGNORED) {
		err = ECANCELED;
		goto out;
	}

	if (mcreceiver->prio >= multicast_callprio() && uag_call_count()) {
		mcreceiver->state = RECEIVING;
		mcplayer_stop();
		goto out;
	}
	else if (mcreceiver->prio < multicast_callprio()) {
		struct le *leua;
		struct ua *ua;

		uag_set_dnd(true);
		uag_set_nodial(true);

		for (leua = list_head(uag_list()); leua; leua = leua->next) {
			struct le *lecall;
			ua = leua->data;
			for (lecall = list_head(ua_calls(ua)); lecall;
				lecall = lecall->next) {
				struct call *call = lecall->data;
				if (!call_is_onhold(call))
					call_hold(call, true);
			}
		}
	}

	le = list_apply(&mcreceivl, true, mcreceiver_running, NULL);
	if (!le) {
		mcreceiver->state = RUNNING;
		mcreceiver->ssrc = ssrc;
		err = player_stop_start(mcreceiver);

		info ("multicast receiver: start addr=%J prio=%d enabled=%d "
			"state=%s\n", &mcreceiver->addr, mcreceiver->prio,
			mcreceiver->enable, state_str(mcreceiver->state));

		module_event("multicast", "receiver start", NULL, NULL,
			"addr=%J prio=%d enabled=%d state=%s",
			&mcreceiver->addr, mcreceiver->prio,
			mcreceiver->enable, state_str(mcreceiver->state));

		goto out;
	}

	hprio = le->data;
	if (hprio->prio < mcreceiver->prio) {
		goto out;
	}

	if (hprio->prio == mcreceiver->prio && mcreceiver->ssrc != ssrc) {
		if (hprio->state == IGNORED)
			hprio->state = RUNNING;

		mcreceiver->ssrc = ssrc;
		err = player_stop_start(mcreceiver);

		info ("multicast receiver: restart addr=%J prio=%d enabled=%d "
			"state=%s\n", &mcreceiver->addr, mcreceiver->prio,
			mcreceiver->enable, state_str(mcreceiver->state));

		module_event("multicast", "receiver restart", NULL, NULL,
			"addr=%J prio=%d enabled=%d state=%s",
			&mcreceiver->addr, mcreceiver->prio,
			mcreceiver->enable, state_str(mcreceiver->state));

		goto out;
	}
	else if (hprio->prio == mcreceiver->prio) {
		goto out;
	}

	hprio->state = RECEIVING;
	mcreceiver->state = RUNNING;
	mcreceiver->ssrc = ssrc;

	err = player_stop_start(mcreceiver);

	info ("multicast receiver: start addr=%J prio=%d enabled=%d "
		"state=%s\n", &mcreceiver->addr, mcreceiver->prio,
		mcreceiver->enable, state_str(mcreceiver->state));

	module_event("multicast", "receiver start", NULL, NULL,
		"addr=%J prio=%d enabled=%d state=%s",
		&mcreceiver->addr, mcreceiver->prio, mcreceiver->enable,
		state_str(mcreceiver->state));

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
	info ("multicast receiver: EOS addr=%J prio=%d enabled=%d state=%s\n",
		&mcreceiver->addr, mcreceiver->prio, mcreceiver->enable,
		state_str(mcreceiver->state));

	module_event("multicast", "receiver EOS", NULL, NULL,
		"addr=%J prio=%d enabled=%d state=%s",
		&mcreceiver->addr, mcreceiver->prio, mcreceiver->enable,
		state_str(mcreceiver->state));

	lock_write_get(mcreceivl_lock);
	if (mcreceiver->state == RUNNING)
		mcplayer_stop();

	mcreceiver->state = LISTENING;
	mcreceiver->ssrc = 0;
	mcreceiver->ac   = 0;
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

	mcreceiver->ac = pt2codec(hdr);
	if (!mcreceiver->ac)
		goto out;

	if (!mbuf_get_left(mb))
		goto out;

	err = prio_handling(mcreceiver, hdr->ssrc);
	if (err)
		goto out;

	err = jbuf_put(mcreceiver->jbuf, hdr, mb);
	if (err)
		return;

  out:
	tmr_start(&mcreceiver->timeout, TIMEOUT, timeout_handler, mcreceiver);

	return;
}


/**
 * udp receive handler
 *
 * @note This is a wrapper function for the RTP receive handler to allow an
 * any port number as receiving port.
 * RTP socket pointer of 0xdeadbeef is a dummy address. The function rtp_decode
 * does nothing on the socket pointer.
 *
 * @param src	src address
 * @param mb	payload buffer
 * @param arg	rtp_handler argument
 */
static void rtp_handler_wrapper(const struct sa *src,
	struct mbuf *mb, void *arg)
{
	int err = 0;
	struct rtp_header hdr;

	err = rtp_decode((struct rtp_sock*)0xdeadbeef, mb, &hdr);
	if (err) {
		warning("multicast receiver: Decoding of rtp (%m)\n", err);
		return;
	}

	rtp_handler(src, &hdr, mb, arg);
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

		if (mcreceiver->prio <= prio) {
			mcreceiver->enable = true;
		}
		else {
			mcreceiver->enable = false;

			if (mcreceiver->state == RUNNING) {
				mcreceiver->state = RECEIVING;
				mcplayer_stop();
			}
		}
	}

	lock_rel(mcreceivl_lock);
	resume_uag_state();
}


/**
 * Enable / Disable a certain priority range
 *
 * @param priol Lower priority boundary
 * @param prioh Higher priority boundary
 * @param en    Enable / Disable flag
 */
void mcreceiver_enrangeprio(uint32_t priol, uint32_t prioh, bool en)
{
	struct le *le;
	struct mcreceiver *mcreceiver;

	if (!priol || !prioh)
		return;

	lock_write_get(mcreceivl_lock);
	LIST_FOREACH(&mcreceivl, le) {
		mcreceiver = le->data;

		if (mcreceiver->prio >=priol && mcreceiver->prio <= prioh) {
			mcreceiver->enable = en;

			if (mcreceiver->state == RUNNING) {
				mcreceiver->state = RECEIVING;
				mcplayer_stop();
			}
		}
	}

	lock_rel(mcreceivl_lock);
	resume_uag_state();
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
		mcreceiver->enable = enable;
	}
	lock_rel(mcreceivl_lock);
	mcplayer_stop();
	resume_uag_state();
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
	resume_uag_state();
	return 0;
}


/**
 * Search and set the ignore flag of the given priority multicast receiver
 *
 * @param prio Priority
 *
 * @return int 0 if success, errorcode otherwise
 */
int mcreceiver_prioignore(uint32_t prio)
{
	struct le *le;
	struct mcreceiver *mcreceiver;
	int err = 0;

	if (!prio)
		return EINVAL;

	le = list_apply(&mcreceivl, true, mcreceiver_prio_cmp, &prio);
	if (!le) {
		warning ("multicast receiver: priority %d not found\n", prio);
		return EINVAL;
	}

	mcreceiver = le->data;
	if (mcreceiver->state == IGNORED)
		return 0;

	lock_write_get(mcreceivl_lock);
	switch (mcreceiver->state) {
		case RUNNING:
			mcreceiver->state = IGNORED;
			mcplayer_stop();
			break;
		case RECEIVING:
			mcreceiver->state = IGNORED;
			break;
		default:
			err = EPERM;
			warning ("multicast receiver: priority %d not"
				" running or receiving(%m)\n", prio, err);
			break;
	}

	lock_rel(mcreceivl_lock);
	resume_uag_state();
	return err;
}


/**
 * Un-register all multicast listener
 */
void mcreceiver_unregall(void)
{
	lock_write_get(mcreceivl_lock);
	list_flush(&mcreceivl);
	lock_rel(mcreceivl_lock);
	resume_uag_state();
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
	lock_rel(mcreceivl_lock);
	mem_deref(mcreceiver);
	resume_uag_state();

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
	struct range jbuf_del;
	uint32_t jbuf_wish;
	enum jbuf_type jbtype;
	struct pl pl;

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

	mcreceiver->enable = true;
	mcreceiver->state = LISTENING;

	jbuf_del  = cfg->jbuf_del;
	jbuf_wish = cfg->jbuf_wish;
	jbtype = cfg->jbtype;
	(void)conf_get_range(conf_cur(), "multicast_jbuf_delay", &jbuf_del);
	if (0 == conf_get(conf_cur(), "multicast_jbuf_type", &pl))
		jbtype = conf_get_jbuf_type(&pl);
	(void)conf_get_u32(conf_cur(), "multicast_jbuf_wish", &jbuf_wish);

	err = jbuf_alloc(&mcreceiver->jbuf, jbuf_del.min, jbuf_del.max);
	err |= jbuf_set_type(mcreceiver->jbuf, jbtype);
	err |= jbuf_set_wish(mcreceiver->jbuf, jbuf_wish);
	if (err)
		goto out;

	err = udp_listen(&mcreceiver->rtp, &mcreceiver->addr,
		rtp_handler_wrapper, mcreceiver);
	if (err) {
		warning("multicast receiver: udp listen failed:"
			"af=%s port=%u-&u (%m)\n", net_af2name(sa_af(addr)),
			port, port + 1, err);
		goto out;
	}

	if (IN_MULTICAST(sa_in(&mcreceiver->addr))) {
		err = udp_multicast_join((struct udp_sock *)
			mcreceiver->rtp, &mcreceiver->addr);
		if (err) {
			warning ("multicast recevier: join multicast group "
				"failed %J (%m)\n", &mcreceiver->addr, err);
			goto out;
		}
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
		re_hprintf(pf, "   addr=%J prio=%d enabled=%d state=%s\n",
			&mcreceiver->addr, mcreceiver->prio,
			mcreceiver->enable, state_str(mcreceiver->state));
	}
}
