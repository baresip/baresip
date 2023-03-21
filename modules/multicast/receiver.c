/**
 * @file receiver.c
 *
 * Copyright (C) 2021 Commend.com - c.huber@commend.com
 */

#include <re.h>
#include <rem.h>
#include <baresip.h>

#include "multicast.h"

#define DEBUG_MODULE "mcreceiver"
#define DEBUG_LEVEL 6
#include <re_dbg.h>


struct list mcreceivl = LIST_INIT;
static mtx_t mcreceivl_lock;


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
	bool muted;
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
	mcplayer_fadeout();
	return mcplayer_start(mcreceiver->ac);
}


static void mcreceiver_stop(struct mcreceiver *mcreceiver)
{
	mcreceiver->state = RECEIVING;

	module_event("multicast",
		     "receiver stopped playing", NULL, NULL,
		     "addr=%J prio=%d enabled=%d state=%s",
		     &mcreceiver->addr, mcreceiver->prio,
		     mcreceiver->enable,
		     state_str(mcreceiver->state));

	jbuf_flush(mcreceiver->jbuf);
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

	err = mtx_trylock(&mcreceivl_lock) != thrd_success;
	if (err)
		return ENOMEM;

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
		if (mcreceiver->state == RUNNING) {
			mcreceiver_stop(mcreceiver);
			mcplayer_stop();
		}
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
			lecall = list_head(ua_calls(ua));
			while (lecall) {
				struct call *call = lecall->data;
				lecall = lecall->next;

				if (call_state(call) !=
					CALL_STATE_ESTABLISHED) {
					ua_hangup(ua, call, 0, NULL);
					continue;
				}

				if (!call_is_onhold(call))
					call_hold(call, true);
			}
		}
	}

	le = list_apply(&mcreceivl, true, mcreceiver_running, NULL);
	if (!le) {
		err = player_stop_start(mcreceiver);
		if (err)
			goto out;

		mcreceiver->state = RUNNING;
		mcreceiver->ssrc = ssrc;

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

		mcplayer_fadein(true);
		mcreceiver->ssrc = ssrc;

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

	err = player_stop_start(mcreceiver);
	if (err)
		goto out;

	hprio->state = RECEIVING;
	jbuf_flush(hprio->jbuf);
	mcreceiver->state = RUNNING;
	mcreceiver->ssrc = ssrc;


	info ("multicast receiver: start addr=%J prio=%d enabled=%d "
		"state=%s\n", &mcreceiver->addr, mcreceiver->prio,
		mcreceiver->enable, state_str(mcreceiver->state));

	module_event("multicast", "receiver start", NULL, NULL,
		"addr=%J prio=%d enabled=%d state=%s",
		&mcreceiver->addr, mcreceiver->prio, mcreceiver->enable,
		state_str(mcreceiver->state));

  out:
	mtx_unlock(&mcreceivl_lock);
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

	mtx_lock(&mcreceivl_lock);
	if (mcreceiver->state == RUNNING) {
		mcplayer_stop();
		jbuf_flush(mcreceiver->jbuf);
	}

	mcreceiver->state = LISTENING;
	mcreceiver->muted = false;
	mcreceiver->ssrc = 0;
	mcreceiver->ac   = 0;
	resume_uag_state();

	mtx_unlock(&mcreceivl_lock);
	return;
}


/**
 * Decode RTP packet
 *
 * @return 0 if success, otherwise errorcode
 */
static int player_decode(struct mcreceiver *mcreceiver)
{
	void *mb = NULL;
	struct rtp_header hdr;
	int jerr;
	int err;

	jerr = jbuf_get(mcreceiver->jbuf, &hdr, &mb);
	if (jerr && jerr != EAGAIN)
		return jerr;

	err = mcplayer_decode(&hdr, mb, jerr == EAGAIN);
	mb = mem_deref(mb);
	if (err)
		return err;

	return jerr;
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

	if (mcreceiver->state == RUNNING) {
		if (mcreceiver->muted && mcplayer_fadeout_done()) {
			mcplayer_stop();
			jbuf_flush(mcreceiver->jbuf);
			goto out;
		}

		err = jbuf_put(mcreceiver->jbuf, hdr, mb);
		if (err)
			return;

		if (player_decode(mcreceiver) == EAGAIN) {
			(void) player_decode(mcreceiver);
		}
	}

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

	mtx_lock(&mcreceivl_lock);
	LIST_FOREACH(&mcreceivl, le) {
		mcreceiver = le->data;

		if (mcreceiver->prio <= prio) {
			mcreceiver->enable = true;
		}
		else {
			mcreceiver->enable = false;

			if (mcreceiver->state == RUNNING) {
				mcreceiver_stop(mcreceiver);
				mcplayer_stop();
			}
		}
	}

	mtx_unlock(&mcreceivl_lock);
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

	mtx_lock(&mcreceivl_lock);
	LIST_FOREACH(&mcreceivl, le) {
		mcreceiver = le->data;

		if (mcreceiver->prio >=priol && mcreceiver->prio <= prioh) {
			mcreceiver->enable = en;

			if (mcreceiver->state == RUNNING) {
				mcreceiver_stop(mcreceiver);
				mcplayer_stop();
			}
		}
	}

	mtx_unlock(&mcreceivl_lock);
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

	mtx_lock(&mcreceivl_lock);
	LIST_FOREACH(&mcreceivl, le) {
		mcreceiver = le->data;
		mcreceiver->enable = enable;
		if (mcreceiver->state == RUNNING)
			mcreceiver_stop(mcreceiver);
	}

	mtx_unlock(&mcreceivl_lock);
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
	mtx_lock(&mcreceivl_lock);
	mcreceiver->prio = prio;
	mtx_unlock(&mcreceivl_lock);
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

	mtx_lock(&mcreceivl_lock);
	switch (mcreceiver->state) {
		case RUNNING:
			mcreceiver->state = IGNORED;
			mcplayer_stop();
			jbuf_flush(mcreceiver->jbuf);
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

	mtx_unlock(&mcreceivl_lock);
	resume_uag_state();
	return err;
}


/**
 * Toggle mute flag of the given priority multicast receiver
 *
 * @param prio Priority
 *
 * @return int 0 if success, errorcode otherwise
 */
int mcreceiver_mute(uint32_t prio)
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
	mtx_lock(&mcreceivl_lock);
	mcreceiver->muted = !mcreceiver->muted;
	if (mcreceiver->state == RUNNING) {
		if (mcreceiver->muted) {
			mcplayer_fadeout();
		}
		else {
			mcplayer_fadein(false);
			err = mcplayer_start(mcreceiver->ac);
			if (err == EINPROGRESS)
				err = 0;
		}
	}
	mtx_unlock(&mcreceivl_lock);
	return err;
}


/**
 * Un-register all multicast listener
 */
void mcreceiver_unregall(void)
{
	mtx_lock(&mcreceivl_lock);
	list_flush(&mcreceivl);
	mtx_unlock(&mcreceivl_lock);
	resume_uag_state();
	mtx_destroy(&mcreceivl_lock);
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
	mtx_lock(&mcreceivl_lock);
	list_unlink(&mcreceiver->le);
	mtx_unlock(&mcreceivl_lock);
	mem_deref(mcreceiver);
	resume_uag_state();

	if (list_isempty(&mcreceivl))
		mtx_destroy(&mcreceivl_lock);
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

	if (list_isempty(&mcreceivl)) {
		err = mtx_init(&mcreceivl_lock, mtx_plain) != thrd_success;
		if (err) {
			err = ENOMEM;
			goto out;
		}
	}

	sa_cpy(&mcreceiver->addr, addr);
	port = sa_port(&mcreceiver->addr);
	mcreceiver->prio = prio;

	mcreceiver->enable = true;
	mcreceiver->muted = false;
	mcreceiver->state = LISTENING;

	jbuf_del  = cfg->jbuf_del;
	jbtype = cfg->jbtype;
	(void)conf_get_range(conf_cur(), "multicast_jbuf_delay", &jbuf_del);
	if (0 == conf_get(conf_cur(), "multicast_jbuf_type", &pl))
		jbtype = conf_get_jbuf_type(&pl);

	err = jbuf_alloc(&mcreceiver->jbuf, jbuf_del.min, jbuf_del.max);
	err |= jbuf_set_type(mcreceiver->jbuf, jbtype);
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

	mtx_lock(&mcreceivl_lock);
	list_append(&mcreceivl, &mcreceiver->le, mcreceiver);
	mtx_unlock(&mcreceivl_lock);

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
		re_hprintf(pf, "   addr=%J prio=%d enabled=%d muted=%d "
			"state=%s\n", &mcreceiver->addr, mcreceiver->prio,
			mcreceiver->enable, mcreceiver->muted,
			state_str(mcreceiver->state));
	}
}
