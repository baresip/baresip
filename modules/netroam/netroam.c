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
};


static struct netroam d;


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


static void poll_changes(void *arg)
{
	struct netroam *n = arg;
	bool changed = false;
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

	if (n->interval)
		tmr_start(&n->tmr, changed ? 1000 : n->interval * 1000,
			  poll_changes, n);
}


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
	d.cfg = &conf_config()->net;
	d.net = baresip_network();
	/* TODO++: Use AF_NETLINK socket to be notified! (man 7 netlink) */
	d.interval = 60;
	conf_get_u32(conf_cur(), "netroam_interval", &d.interval);
	if (d.interval)
		tmr_start(&d.tmr, d.interval * 1000, poll_changes, &d);

	return cmd_register(baresip_commands(), cmdv, ARRAY_SIZE(cmdv));
}


static int module_close(void)
{
	tmr_cancel(&d.tmr);
	cmd_unregister(baresip_commands(), cmdv);
	return 0;
}


const struct mod_export DECL_EXPORTS(netroam) = {
	"netroam",
	"application",
	module_init,
	module_close
};
