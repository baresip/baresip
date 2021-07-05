/**
 * @file src/net.c Networking code
 *
 * Copyright (C) 2010 - 2016 Alfred E. Heggestad
 */
#include <re.h>
#include <baresip.h>
#include "core.h"


struct network {
	struct config_net cfg;
	bool   changed;               /**< Changed flag for list laddrs      */
	struct tmr tmr;
	struct dnsc *dnsc;
	struct sa nsv[NET_MAX_NS];/**< Configured name servers           */
	uint32_t nsn;        /**< Number of configured name servers      */
	struct sa nsvf[NET_MAX_NS];/**< Configured fallback name servers */
	uint32_t nsnf;       /**< Number of configured fallback name servers */
	uint32_t interval;
	net_change_h *ch;
	void *arg;
};


struct net_ifaddr {
	const struct network *net;
	net_ifaddr_h *ifh;            /**< Interface address handler         */
	void *arg;
};


enum laddr_check {
	LADDR_NOLINKLOCAL = 1,
	LADDR_INTERNET = 2
};


struct laddr_filter {
	const struct network *net;
	enum laddr_check lc;
	struct sa dst;
	struct sa *laddr;
};


static void net_destructor(void *data)
{
	struct network *net = data;

	tmr_cancel(&net->tmr);
	mem_deref(net->dnsc);
}


static int net_dns_srv_add(struct network *net, const struct sa *sa,
		bool fallback)
{
	if (!net)
		return EINVAL;

	if (!fallback && net->nsn >= ARRAY_SIZE(net->nsv))
		return E2BIG;

	if (fallback && net->nsnf >= ARRAY_SIZE(net->nsvf))
		return E2BIG;

	if (fallback)
		sa_cpy(&net->nsvf[net->nsnf++], sa);
	else
		sa_cpy(&net->nsv[net->nsn++], sa);

	return 0;
}


static int net_dns_srv_get(const struct network *net,
			   struct sa *srvv, uint32_t *n, bool *from_sys)
{
	struct sa nsv[NET_MAX_NS];
	uint32_t i, nsn = ARRAY_SIZE(nsv);
	uint32_t offset;
	uint32_t limit = *n;
	int err;

	err = dns_srv_get(NULL, 0, nsv, &nsn);
	if (err) {
		nsn = 0;
	}

	if (net->nsn) {

		if (net->nsn > limit)
			return E2BIG;

		/* Use any configured nameservers */
		for (i=0; i<net->nsn; i++) {
			srvv[i] = net->nsv[i];
		}

		*n = net->nsn;

		if (from_sys)
			*from_sys = false;
	}
	else {
		if (nsn > limit)
			return E2BIG;

		for (i=0; i<nsn; i++)
			srvv[i] = nsv[i];

		*n = nsn;

		if (from_sys)
			*from_sys = true;
	}

	/* Add Fallback nameservers */
	if (net->nsnf) {
		offset = *n;
		if ((offset + net->nsnf) > limit) {
			debug("net: too many DNS nameservers, "
					"fallback DNS ignored\n");
			return 0;
		}

		for (i=0; i<net->nsnf; i++) {
			srvv[offset+i] = net->nsvf[i];
		}

		*n = offset + net->nsnf;
	}

	return 0;
}


/*
 * Check for DNS Server updates
 */
void net_dns_refresh(struct network *net)
{
	struct sa nsv[NET_MAX_NS];
	uint32_t nsn;
	int err;

	nsn = ARRAY_SIZE(nsv);

	err = net_dns_srv_get(net, nsv, &nsn, NULL);
	if (err)
		return;

	(void)dnsc_srv_set(net->dnsc, nsv, nsn);
}


/*
 * Detect changes in IP address(es)
 */
static void ipchange_handler(void *arg)
{
	struct network *net = arg;
	bool change;

	tmr_start(&net->tmr, net->interval * 1000, ipchange_handler, net);

	net_dns_refresh(net);

	change = net_check(net);
	if (change && net->ch) {
		net->ch(net->arg);
	}
}


static bool net_ifaddr_filter(const struct network *net, const char *ifname,
			      const struct sa *sa)
{
	const struct config_net *cfg = &net->cfg;

	if (!sa_isset(sa, SA_ADDR))
		return false;

	if (str_isset(cfg->ifname) && str_cmp(cfg->ifname, ifname))
		return false;

	if (!net_af_enabled(net, sa_af(sa)))
		return false;

	if (sa_is_loopback(sa))
		return false;

	return true;
}


static bool net_misses_laddr(const char *ifname, const struct sa *sa,
			     void *arg)
{
	struct network *net = arg;
	struct sa sac;

	net->changed = false;
	if (!net_ifaddr_filter(net, ifname, sa))
		return false;

	sa_cpy(&sac, sa);
	net->changed = !uag_transp_isladdr(&sac);
	return net->changed;
}


static int net_dst_is_source_addr(const struct sa *dst, const struct sa *ip)
{
	struct sa src;
	int err;

	err = net_dst_source_addr_get(dst, &src);
	if (err)
		return err;

	if (!sa_cmp(ip, &src, SA_ADDR))
		return ECONNREFUSED;

	return 0;
}


/**
 * Check if local IP address(es) changed
 *
 * @param net Network instance
 *
 * @return True if changed, otherwise false
 */
bool net_check(struct network *net)
{
	const struct config_net *cfg = &net->cfg;
	struct sa sa;

	if (!net)
		return false;

	if (str_isset(cfg->ifname) && 0 == sa_set_str(&sa, cfg->ifname, 0)) {
		debug("Binding to IP address '%j'\n", &sa);
		return false;
	}

	net_if_apply(net_misses_laddr, net);
	net->changed |= uag_transp_obsolete();
	return net->changed;
}


/**
 * Check if address family is enabled
 *
 * @param net Network instance
 * @param af  AF_INET or AF_INET6
 *
 * @return True if enabled, false if disabled
 */
bool net_af_enabled(const struct network *net, int af)
{
	if (!net || af == AF_UNSPEC)
		return false;

	switch (net->cfg.af) {

	case AF_UNSPEC:
		return true;

	default:
		return af == net->cfg.af;
	}
}


static int dns_init(struct network *net)
{
	struct sa nsv[NET_MAX_NS];
	uint32_t nsn = ARRAY_SIZE(nsv);
	int err;

	err = net_dns_srv_get(net, nsv, &nsn, NULL);
	if (err)
		return err;

	return dnsc_alloc(&net->dnsc, NULL, nsv, nsn);
}


/**
 * Return TRUE if libre supports IPv6
 */
static bool check_ipv6(void)
{
	struct sa sa;

	return 0 == sa_set_str(&sa, "::1", 2000);
}


/**
 * Initialise networking
 *
 * @param netp Pointer to allocated network instance
 * @param cfg  Network configuration
 *
 * @return 0 if success, otherwise errorcode
 */
int net_alloc(struct network **netp, const struct config_net *cfg)
{
	struct network *net;
	struct sa nsv[NET_MAX_NS];
	uint32_t nsn = ARRAY_SIZE(nsv);
	int err;

	if (!netp || !cfg)
		return EINVAL;

	/*
	 * baresip/libre must be built with matching HAVE_INET6 value.
	 * if different the size of `struct sa' will not match and the
	 * application is very likely to crash.
	 */
#ifdef HAVE_INET6
	if (!check_ipv6()) {
		warning("libre was compiled without IPv6-support"
			", but baresip was compiled with\n");
		return EAFNOSUPPORT;
	}
#else
	if (check_ipv6()) {
		warning("libre was compiled with IPv6-support"
			", but baresip was compiled without\n");
		return EAFNOSUPPORT;
	}
#endif

	net = mem_zalloc(sizeof(*net), net_destructor);
	if (!net)
		return ENOMEM;

	net->cfg = *cfg;

	tmr_init(&net->tmr);

	if (cfg->nsc) {
		size_t i;

		for (i=0; i<cfg->nsc; i++) {

			const char *ns = cfg->nsv[i].addr;
			struct sa sa;

			err = sa_decode(&sa, ns, str_len(ns));
			if (err) {
				warning("net: dns_server:"
					" could not decode `%s' (%m)\n",
					ns, err);
				goto out;
			}

			err = net_dns_srv_add(net, &sa, cfg->nsv[i].fallback);
			if (err) {
				warning("net: failed to add nameserver: %m\n",
					err);
				goto out;
			}
		}
	}

	/* Initialise DNS resolver */
	err = dns_init(net);
	if (err) {
		warning("net: dns_init: %m\n", err);
		goto out;
	}

	(void)dns_srv_get(NULL, 0, nsv, &nsn);

 out:
	if (err)
		mem_deref(net);
	else
		*netp = net;

	return err;
}


/**
 * Use a specific DNS server
 *
 * @param net  Network instance
 * @param srvv DNS Nameservers
 * @param srvc Number of nameservers
 *
 * @return 0 if success, otherwise errorcode
 */
int net_use_nameserver(struct network *net, const struct sa *srvv, size_t srvc)
{
	size_t i;

	if (!net)
		return EINVAL;

	net->nsn = (uint32_t)min(ARRAY_SIZE(net->nsv), srvc);

	if (srvv) {
		for (i=0; i<srvc; i++) {
			net->nsv[i] = srvv[i];
		}
	}

	net_dns_refresh(net);

	return 0;
}


/**
 * Set network IP address
 *
 * @param net  Network instance
 * @param ip   IP address
 *
 * @return 0 if success, otherwise errorcode
 */
int net_set_address(struct network *net, const struct sa *ip)
{
	int err;
	if (!net || !sa_isset(ip, SA_ADDR))
		return EINVAL;

	err = re_snprintf(net->cfg.ifname, sizeof(net->cfg.ifname), "%j", ip);
	if (err)
		return err;

	return uag_reset_transp(true, true);
}


/**
 * Check for networking changes with a regular interval
 *
 * @param net       Network instance
 * @param interval  Interval in seconds
 * @param ch        Handler called when a change was detected
 * @param arg       Handler argument
 */
void net_change(struct network *net, uint32_t interval,
		net_change_h *ch, void *arg)
{
	if (!net)
		return;

	net->interval = interval;
	net->ch = ch;
	net->arg = arg;

	if (interval)
		tmr_start(&net->tmr, interval * 1000, ipchange_handler, net);
	else
		tmr_cancel(&net->tmr);
}


/**
 * Force a change in the network interfaces
 *
 * @param net Network instance
 */
void net_force_change(struct network *net)
{
	if (net && net->ch) {
		net->ch(net->arg);
	}
}


/**
 * Print DNS server debug information
 *
 * @param pf     Print handler for debug output
 * @param net    Network instance
 *
 * @return 0 if success, otherwise errorcode
 */
int net_dns_debug(struct re_printf *pf, const struct network *net)
{
	struct sa nsv[NET_MAX_NS];
	uint32_t i, nsn = ARRAY_SIZE(nsv);
	bool from_sys = false;
	int err;

	if (!net)
		return 0;

	err = net_dns_srv_get(net, nsv, &nsn, &from_sys);
	if (err)
		nsn = 0;

	err = re_hprintf(pf, " DNS Servers from %s: (%u)\n",
			 from_sys ? "System" : "Config", nsn);
	for (i=0; i<nsn; i++)
		err |= re_hprintf(pf, "   %u: %J\n", i, &nsv[i]);

	return err;
}


/**
 * Set the enabled address family (AF)
 *
 * @param net Network instance
 * @param af  Enabled address family
 *
 * @return 0 if success, otherwise errorcode
 */
int net_set_af(struct network *net, int af)
{
	if (af != AF_INET && af != AF_INET6 && af != AF_UNSPEC)
		return EAFNOSUPPORT;

	if (net)
		net->cfg.af = af;

	return 0;
}


static bool if_debug_handler(const char *ifname, const struct sa *sa,
			     void *arg)
{
	void **argv = arg;
	struct re_printf *pf = argv[0];
	struct network *net = argv[1];
	int err = 0;

	if (net_af_enabled(net, sa_af(sa)))
		err = re_hprintf(pf, " %10s:  %j\n", ifname, sa);

	return err != 0;
}


static bool find_laddr_filter(const char *ifname, const struct sa *sa,
			      void *arg)
{
	struct laddr_filter *f = arg;
	(void)ifname;

	if (!net_ifaddr_filter(f->net, ifname, sa))
		return false;

	if (sa_af(sa) != sa_af(&f->dst))
		return false;

	if ((f->lc & LADDR_NOLINKLOCAL) && sa_is_linklocal(sa))
		return false;

	if ((f->lc & LADDR_INTERNET) &&
				net_dst_is_source_addr(&f->dst, sa))
		return false;

	sa_cpy(f->laddr, sa);
	return true;
}


/**
 * Get the local IP Address for a specific Address Family (AF)
 *
 * @param net Network instance
 * @param af  Address Family
 *
 * @return Local IP Address
 */
int net_laddr_af(const struct network *net, int af, struct sa *laddr)
{
	const struct config_net *cfg = &net->cfg;
	struct laddr_filter lf;

	if (!net || !laddr)
		return EINVAL;

	if (str_isset(cfg->ifname) && 0 == sa_set_str(laddr, cfg->ifname, 0)) {
		if (sa_af(laddr) != af)
			return EINVAL;

		return 0;
	}

	lf.net = net;
	lf.laddr = laddr;
	sa_init(&lf.dst, af);
	if (af == AF_INET6)
		sa_set_str(&lf.dst, "1::1", 53);
	else
		sa_set_str(&lf.dst, "1.1.1.1", 53);

	lf.lc = LADDR_NOLINKLOCAL | LADDR_INTERNET;
	net_if_apply(find_laddr_filter, &lf);
	if (sa_isset(lf.laddr, SA_ADDR))
		return 0;

	lf.lc = LADDR_NOLINKLOCAL;
	net_if_apply(find_laddr_filter, &lf);
	if (sa_isset(lf.laddr, SA_ADDR))
		return 0;

	lf.lc = 0;
	net_if_apply(find_laddr_filter, &lf);
	if (sa_isset(lf.laddr, SA_ADDR))
		return 0;

	return ENOENT;
}


/**
 * Get the DNS Client
 *
 * @param net Network instance
 *
 * @return DNS Client
 */
struct dnsc *net_dnsc(const struct network *net)
{
	if (!net)
		return NULL;

	return net->dnsc;
}


static bool handle_addr(const char *ifname, const struct sa *sa, void *arg)
{
	struct net_ifaddr *nif = arg;
	if (net_ifaddr_filter(nif->net, ifname, sa))
		return nif->ifh(ifname, sa, nif->arg);

	return false;
}


void net_laddr_apply(const struct network *net, net_ifaddr_h *ifh, void *arg)
{
	struct sa sa;
	struct net_ifaddr nif;
	const struct config_net *cfg = &net->cfg;
	if (!net || !ifh)
		return;

	if (str_isset(cfg->ifname) && 0 == sa_set_str(&sa, cfg->ifname, 0)) {
		info("Binding to IP address '%j'\n", &sa);
		ifh(NULL, &sa, NULL);
		return;
	}

	nif.net = net;
	nif.ifh = ifh;
	nif.arg = arg;
	net_if_apply(handle_addr, &nif);
}


/**
 * Print networking debug information
 *
 * @param pf     Print handler for debug output
 * @param net    Network instance
 *
 * @return 0 if success, otherwise errorcode
 */
int net_debug(struct re_printf *pf, const struct network *net)
{
	void *argv[2] = {pf, (void *)net};
	int err;

	if (!net)
		return 0;

	err  = re_hprintf(pf, "--- Network debug ---\n");
	err |= re_hprintf(pf, "net interfaces:\n");
	err |= net_if_apply(if_debug_handler, argv);

	err |= net_dns_debug(pf, net);

	return err;
}
