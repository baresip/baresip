/**
 * @file net.c Network change detection module
 *
 * Copyright (C) 2021 Commend.com - c.spielberger@commend.com
 */

#include <stdlib.h>
#include <string.h>
#include <re.h>
#include <baresip.h>


/**
 * @defgroup netcheck netcheck
 *
 * The network check structure
 *
 */

struct netcheck {
	const struct config_net *cfg;
	struct network *net;
	uint32_t interval;
	struct tmr tmr;
	struct sa laddr;
};


static struct netcheck d;


static bool laddr_obsolete(enum sip_transp tp, const struct sa *laddr,
			   void *arg)
{
	struct netcheck *n = arg;
	char ifname[256] = "???";
	int err;
	(void) tp;

	err = net_if_getname(ifname, sizeof(ifname), sa_af(laddr), laddr);
	if (err == ENODEV) {
		sa_cpy(&n->laddr, laddr);
		return true;
	}

	return false;
}


static bool laddr_find(enum sip_transp tp, const struct sa *laddr, void *arg)
{
	const struct sa *sa = arg;
	(void) tp;

	return sa_cmp(sa, laddr, SA_ADDR);
}


static bool netcheck_find_obsolete(struct netcheck *n)
{
	sip_transp_list(uag_sip(), laddr_obsolete, n);
	return sa_isset(&n->laddr, SA_ADDR);
}


static bool sip_transp_misses_laddr(const char *ifname, const struct sa *sa,
			     void *arg)
{
	struct netcheck *n = arg;

	if (!net_ifaddr_filter(baresip_network(), ifname, sa))
		return false;

	if (!sip_transp_list(uag_sip(), laddr_find, (void*) sa)) {
		sa_cpy(&n->laddr, sa);
		return true;
	}

	return false;
}


static void poll_changes(void *arg)
{
	struct netcheck *n = arg;
	bool changed = false;
	net_dns_refresh(baresip_network());

	/* was a local IP added? */
	sa_init(&n->laddr, AF_UNSPEC);
	net_if_apply(sip_transp_misses_laddr, n);
	if (sa_isset(&n->laddr, SA_ADDR)) {
		debug("netcheck: new IP address %j\n", &n->laddr);
		uag_transp_add(&n->laddr);
		changed = true;
	}

	/* was a local IP removed? */
	sa_init(&n->laddr, AF_UNSPEC);
	if (netcheck_find_obsolete(n)) {
		debug("netcheck: IP address %j was removed\n", &n->laddr);
		uag_transp_rm(&n->laddr);
		changed = true;
	}

	tmr_start(&n->tmr, changed ? 1000 : n->interval * 1000,
		  poll_changes, n);
}


static int module_init(void)
{
	int err = 0;
	d.cfg = &conf_config()->net;
	d.net = baresip_network();
	d.interval = 2;
	tmr_start(&d.tmr, d.interval * 1000, poll_changes, &d);

	return err;
}


static int module_close(void)
{
	tmr_cancel(&d.tmr);
	return 0;
}


const struct mod_export DECL_EXPORTS(netcheck) = {
	"netcheck",
	"application",
	module_init,
	module_close
};
