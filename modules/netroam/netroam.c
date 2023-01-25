/**
 * @file netroam.c Network roaming module
 *
 * Detects and applies changes of the local network addresses
 *
 * Copyright (C) 2021 Commend.com - c.spielberger@commend.com
 */

#include <stdlib.h>
#include <string.h>
#include <re.h>
#include <baresip.h>

#ifdef ADD_NETLINK
#include "netlink.h"
#endif

/**
 * @defgroup netroam netroam
 *
 * The network check structure
 *
 */

struct netroam {
	const struct config_net *cfg;
	struct network *net;
	uint32_t interval;
	struct tmr tmr;
	struct sa laddr;
	bool reset;
	uint32_t failc;                  /**< Fail count                     */
};


static struct netroam d;


static uint32_t failwait(uint32_t failc)
{
	uint32_t maxw = d.interval ? d.interval : 60;
	uint32_t w;

	w = min(maxw, (uint32_t) (1 << min(failc, 6))) * 1000;
	return w;
}


static bool laddr_obsolete(const char *ifname, const struct sa *laddr,
			   void *arg)
{
	struct netroam *n = arg;
	char ifn[2] = "?";
	int err;
	(void) ifname;

	err = net_if_getname(ifn, sizeof(ifn), sa_af(laddr), laddr);
	if (err == ENODEV) {
		sa_cpy(&n->laddr, laddr);
		return true;
	}

	return false;
}


static bool laddr_find(const char *ifname, const struct sa *laddr, void *arg)
{
	const struct sa *sa = arg;
	(void) ifname;

	return sa_cmp(sa, laddr, SA_ADDR);
}


static bool netroam_find_obsolete(struct netroam *n)
{
	sa_init(&n->laddr, AF_UNSPEC);
	net_laddr_apply(n->net, laddr_obsolete, n);
	return sa_isset(&n->laddr, SA_ADDR);
}


static bool net_misses_laddr(const char *ifname, const struct sa *sa,
			     void *arg)
{
	struct netroam *n = arg;

	if (!net_ifaddr_filter(baresip_network(), ifname, sa))
		return false;

	if (!net_laddr_apply(n->net, laddr_find, (void *) sa)) {
		sa_cpy(&n->laddr, sa);
		return true;
	}

	return false;
}


static bool print_addr(const char *ifname, const struct sa *sa, void *arg)
{
	(void) arg;

	re_printf(" %10s:  %j\n", ifname, sa);
	return false;
}


static void print_changes(struct netroam *n)
{
	info("Network changed:\n");
	net_laddr_apply(n->net, print_addr, NULL);
}


static void poll_changes(void *arg)
{
	struct netroam *n = arg;
	bool changed = false;
	int err;

	if (!n->cfg->nsc)
		net_dns_refresh(baresip_network());

	/* was a local IP added? */
	sa_init(&n->laddr, AF_UNSPEC);
	net_if_apply(net_misses_laddr, n);
	if (sa_isset(&n->laddr, SA_ADDR)) {
		net_add_address(n->net, &n->laddr);
		changed = true;
	}

	/* was a local IP removed? */
	sa_init(&n->laddr, AF_UNSPEC);
	if (netroam_find_obsolete(n)) {
		net_rm_address(n->net, &n->laddr);
		changed = true;
	}

	if (!changed && n->reset) {
		print_changes(n);
		err = uag_reset_transp(true, true);
		if (err) {
			warning("netroam: could not reset transport\n");
			module_event("netroam", "could not reset transport",
				     NULL, NULL, "failc=%u (%m)", d.failc,
				     err);
			tmr_start(&n->tmr, failwait(++d.failc), poll_changes,
				  n);
			return;
		}
		else
			n->reset = false;
	}

	d.failc = 0;
	if (changed) {
		n->reset = true;
		tmr_start(&n->tmr, 1000, poll_changes, n);
	}
	else if (n->interval) {
		tmr_start(&n->tmr, n->interval * 1000, poll_changes, n);
	}
}


#ifdef ADD_NETLINK
static void netlink_handler(void *arg)
{
	struct netroam *n = arg;

	tmr_start(&n->tmr, 1000, poll_changes, n);
}
#endif


static int cmd_netchange(struct re_printf *pf, void *unused)
{
	(void)unused;

	re_hprintf(pf, "netroam: network change\n");
	poll_changes(&d);
	return 0;
}


static const struct cmd cmdv[] = {

{"netchange",     0, 0, "Inform netroam about a network change",
				cmd_netchange                                },
};


static int module_init(void)
{
#ifdef ADD_NETLINK
	int err;
#endif

	d.cfg = &conf_config()->net;
	d.net = baresip_network();
	d.interval = 60;
	conf_get_u32(conf_cur(), "netroam_interval", &d.interval);
	if (d.interval)
		tmr_start(&d.tmr, d.interval * 1000, poll_changes, &d);

#ifdef ADD_NETLINK
	err = open_netlink(netlink_handler, &d);
	if (err)
		return err;
#endif
	return cmd_register(baresip_commands(), cmdv, RE_ARRAY_SIZE(cmdv));
}


static int module_close(void)
{
	tmr_cancel(&d.tmr);
	cmd_unregister(baresip_commands(), cmdv);
#ifdef ADD_NETLINK
	close_netlink();
#endif
	return 0;
}


const struct mod_export DECL_EXPORTS(netroam) = {
	"netroam",
	"application",
	module_init,
	module_close
};
