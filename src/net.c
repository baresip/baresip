/**
 * @file src/net.c Networking code
 *
 * Copyright (C) 2010 - 2016 Creytiv.com
 */
#include <re.h>
#include <baresip.h>
#include "core.h"


struct network {
	struct config_net cfg;
	struct sa laddr;
#ifdef HAVE_INET6
	struct sa laddr6;
#endif
	struct tmr tmr;
	struct dnsc *dnsc;
	struct sa nsv[NET_MAX_NS];/**< Configured name servers           */
	uint32_t nsn;        /**< Number of configured name servers      */
	struct sa nsvf[NET_MAX_NS];/**< Configured fallback name servers */
	uint32_t nsnf;       /**< Number of configured fallback name servers */
	uint32_t interval;
	char domain[64];     /**< DNS domain from network                */
	net_change_h *ch;
	void *arg;
};


struct ifentry {
	int af;
	char *ifname;
	struct sa *ip;
	size_t sz;
	bool found;
};


static void net_destructor(void *data)
{
	struct network *net = data;

	tmr_cancel(&net->tmr);
	mem_deref(net->dnsc);
}


static bool if_getname_handler(const char *ifname, const struct sa *sa,
			       void *arg)
{
	struct ifentry *ife = arg;

	if (ife->af != sa_af(sa))
		return false;

	if (sa_cmp(sa, ife->ip, SA_ADDR)) {
		str_ncpy(ife->ifname, ifname, ife->sz);
		ife->found = true;
		return true;
	}

	return false;
}


static int network_if_getname(char *ifname, size_t sz,
			      int af, const struct sa *ip)
{
	struct ifentry ife;
	int err;

	if (!ifname || !sz || !ip)
		return EINVAL;

	ife.af     = af;
	ife.ifname = ifname;
	ife.ip     = (struct sa *)ip;
	ife.sz     = sz;
	ife.found  = false;

	err = net_if_apply(if_getname_handler, &ife);

	return ife.found ? err : ENODEV;
}


static int print_addr(struct re_printf *pf, const struct sa *ip)
{
	if (!ip)
		return 0;

	if (sa_isset(ip, SA_ADDR)) {

		char ifname[256] = "???";

		network_if_getname(ifname, sizeof(ifname), sa_af(ip), ip);

		return re_hprintf(pf, "%s|%j", ifname, ip);
	}
	else {
		return re_hprintf(pf, "(not set)");
	}
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
			warning("net: too many DNS nameservers, "
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
static void dns_refresh(struct network *net)
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

	dns_refresh(net);

	change = net_check(net);
	if (change && net->ch) {
		net->ch(net->arg);
	}
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
	struct sa laddr;
#ifdef HAVE_INET6
	struct sa laddr6;
#endif
	bool change = false;

	if (!net)
		return false;

	laddr = net->laddr;
#ifdef HAVE_INET6
	laddr6 = net->laddr6;
#endif

	if (str_isset(net->cfg.ifname)) {

		if (net_af_enabled(net, AF_INET))
			net_if_getaddr(net->cfg.ifname, AF_INET, &net->laddr);

#ifdef HAVE_INET6
		if (net_af_enabled(net, AF_INET6))
			net_if_getaddr(net->cfg.ifname, AF_INET6,
				       &net->laddr6);
#endif
	}
	else {
		if (net_af_enabled(net, AF_INET))
			net_default_source_addr_get(AF_INET, &net->laddr);

#ifdef HAVE_INET6
		if (net_af_enabled(net, AF_INET6))
			net_default_source_addr_get(AF_INET6, &net->laddr6);
#endif
	}

	if (sa_isset(&net->laddr, SA_ADDR) &&
	    !sa_cmp(&laddr, &net->laddr, SA_ADDR)) {
		change = true;
		info("net: local IPv4 address changed: %j -> %j\n",
		     &laddr, &net->laddr);
	}

#ifdef HAVE_INET6
	if (sa_isset(&net->laddr6, SA_ADDR) &&
	    !sa_cmp(&laddr6, &net->laddr6, SA_ADDR)) {
		change = true;
		info("net: local IPv6 address changed: %j -> %j\n",
		     &laddr6, &net->laddr6);
	}
#endif

	return change;
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
	char buf4[128] = "", buf6[128] = "";
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

	sa_init(&net->laddr, AF_INET);

	if (str_isset(cfg->ifname)) {

		struct sa temp_sa;
		bool got_it = false;

		info("Binding to interface or IP address '%s'\n", cfg->ifname);

		/* check for valid IP-address */
		if (0 == sa_set_str(&temp_sa, cfg->ifname, 0)) {

			switch (sa_af(&temp_sa)) {

			case AF_INET:
				net->laddr = temp_sa;
				break;

#ifdef HAVE_INET6
			case AF_INET6:
				net->laddr6 = temp_sa;
				break;
#endif

			default:
				err = EAFNOSUPPORT;
				goto out;
			}

			goto print_network_data;
		}

		if (net_af_enabled(net, AF_INET)) {

			err = net_if_getaddr(cfg->ifname, AF_INET,
					     &net->laddr);
			if (err) {
				info("net: %s: could not get IPv4 address"
				     " (%m)\n",
				     cfg->ifname, err);
			}
			else
				got_it = true;
		}

#ifdef HAVE_INET6
		if (net_af_enabled(net, AF_INET6)) {

			err = net_if_getaddr(cfg->ifname, AF_INET6,
					     &net->laddr6);
			if (err) {
				info("net: %s: could not get IPv6 address"
				     " (%m)\n",
				     cfg->ifname, err);
			}
			else
				got_it = true;
		}
#endif
		if (got_it)
			err = 0;
		else {
			warning("net: %s: could not get network address\n",
				cfg->ifname);
			err = EADDRNOTAVAIL;
			goto out;
		}
	}
	else {
		if (net_af_enabled(net, AF_INET))
			net_default_source_addr_get(AF_INET, &net->laddr);

#ifdef HAVE_INET6
		sa_init(&net->laddr6, AF_INET6);

		if (net_af_enabled(net, AF_INET6))
			net_default_source_addr_get(AF_INET6, &net->laddr6);
#endif
	}

print_network_data:

	if (sa_isset(&net->laddr, SA_ADDR)) {
		re_snprintf(buf4, sizeof(buf4), " IPv4=%H",
			    print_addr, &net->laddr);
	}
#ifdef HAVE_INET6
	if (sa_isset(&net->laddr6, SA_ADDR)) {
		re_snprintf(buf6, sizeof(buf6), " IPv6=%H",
			    print_addr, &net->laddr6);
	}
#endif

	(void)dns_srv_get(net->domain, sizeof(net->domain), nsv, &nsn);

	info("Local network address: %s %s\n", buf4, buf6);

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

	dns_refresh(net);

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
	if (!net)
		return EINVAL;

	switch (sa_af(ip)) {

	case AF_INET:
		sa_cpy(&net->laddr, ip);
		break;

#ifdef HAVE_INET6
	case AF_INET6:
		sa_cpy(&net->laddr6, ip);
		break;
#endif
	default:
		return EAFNOSUPPORT;
	}

	return 0;
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


/**
 * Get the local IP Address for a specific Address Family (AF)
 *
 * @param net Network instance
 * @param af  Address Family
 *
 * @return Local IP Address
 */
const struct sa *net_laddr_af(const struct network *net, int af)
{
	if (!net)
		return NULL;

	switch (af) {

	case AF_INET:  return &net->laddr;
#ifdef HAVE_INET6
	case AF_INET6: return &net->laddr6;
#endif
	default:       return NULL;
	}
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


/**
 * Get the network domain name
 *
 * @param net Network instance
 *
 * @return Network domain
 */
const char *net_domain(const struct network *net)
{
	if (!net)
		return NULL;

	return net->domain[0] ? net->domain : NULL;
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
	err |= re_hprintf(pf, " Local IPv4:  [%s] %H\n",
			  net_af_enabled(net, AF_INET) ? "E" : ".",
			  print_addr, &net->laddr);
#ifdef HAVE_INET6
	err |= re_hprintf(pf, " Local IPv6:  [%s] %H\n",
			  net_af_enabled(net, AF_INET6) ? "E" : ".",
			  print_addr, &net->laddr6);
#endif
	err |= re_hprintf(pf, " Domain: %s\n", net->domain);

	err |= re_hprintf(pf, "net interfaces:\n");
	err |= net_if_apply(if_debug_handler, argv);

	err |= net_dns_debug(pf, net);

	return err;
}
