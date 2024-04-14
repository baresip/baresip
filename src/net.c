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
	struct list laddrs;       /**< List of local addresses           */

	struct dnsc *dnsc;
	struct sa nsv[NET_MAX_NS];/**< Configured name servers           */
	uint32_t nsn;        /**< Number of configured name servers      */
	struct sa nsvf[NET_MAX_NS];/**< Configured fallback name servers */
	uint32_t nsnf;       /**< Number of configured fallback name servers */
};


struct laddr {
	struct le le;

	char *ifname;
	struct sa sa;
};


static void net_destructor(void *data)
{
	struct network *net = data;

	mem_deref(net->dnsc);
	list_flush(&net->laddrs);
}


static void laddr_destructor(void *data)
{
	struct laddr *laddr = data;

	list_unlink(&laddr->le);
	mem_deref(laddr->ifname);
}


static int net_dns_srv_add(struct network *net, const struct sa *sa,
		bool fallback)
{
	if (!net)
		return EINVAL;

	if (!fallback && net->nsn >= RE_ARRAY_SIZE(net->nsv))
		return E2BIG;

	if (fallback && net->nsnf >= RE_ARRAY_SIZE(net->nsvf))
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
	uint32_t i, nsn = RE_ARRAY_SIZE(nsv);
	uint32_t offset;
	uint32_t limit = *n;
	int err;

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
		err = dns_srv_get(NULL, 0, nsv, &nsn);
		if (err)
			nsn = 0;

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

	nsn = RE_ARRAY_SIZE(nsv);

	err = net_dns_srv_get(net, nsv, &nsn, NULL);
	if (err)
		return;

	(void)dnsc_srv_set(net->dnsc, nsv, nsn);
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
	uint32_t nsn = RE_ARRAY_SIZE(nsv);
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
 * Add a local IP address with given interface name
 *
 * @param net    Network instance
 * @param sa     IP address
 * @param ifname Interface name
 *
 * @return 0 if success, otherwise errorcode
 */
int  net_add_address_ifname(struct network *net, const struct sa *sa,
			    const char *ifname)
{
	struct le *le;
	struct laddr *laddr;
	int err = 0;

	if (!net || !str_isset(ifname) || !sa)
		return EINVAL;

	LIST_FOREACH(&net->laddrs, le) {
		laddr = le->data;
		if (sa_cmp(&laddr->sa, sa, SA_ADDR))
			goto out;
	}

	laddr = mem_zalloc(sizeof(*laddr), laddr_destructor);
	if (!laddr)
		return ENOMEM;

	laddr->sa = *sa;
	err = str_dup(&laddr->ifname, ifname);
	if (err)
		goto out;

	list_append(&net->laddrs, &laddr->le, laddr);

out:
	if (err)
		mem_deref(laddr);

	return err;
}


static bool add_laddr_filter(const char *ifname, const struct sa *sa,
			     void *arg)
{
	struct network *net = arg;

	if (!net_ifaddr_filter(net, ifname, sa))
		return false;

	(void)net_add_address_ifname(net, sa, ifname);
	return false;
}


static bool if_debug_handler(const char *ifname, const struct sa *sa,
			     void *arg)
{
	void **argv = arg;
	struct re_printf *pf = argv[0];
	struct network *net = argv[1];
	bool def;
	int err = 0;

	def = sa_cmp(net_laddr_af(baresip_network(), sa_af(sa)), sa, SA_ADDR);

	if (net_af_enabled(net, sa_af(sa)))
		err = re_hprintf(pf, " %10s:  %j %s\n", ifname, sa,
				 def ? "(default)" : "");

	return err != 0;
}


static int check_route(const struct sa *src, const struct sa *dst)
{
	struct sa ip;
	int err;

	err = net_dst_source_addr_get(dst, &ip);
	if (err)
		return err;

	if (!sa_cmp(src, &ip, SA_ADDR))
		return ECONNREFUSED;

	return 0;
}


enum laddr_check {
	LADDR_NOLINKLOCAL = 1,
	LADDR_INTERNET = 2
};


static const struct sa *find_laddr_af(const struct network *net, int af,
		enum laddr_check lc)
{
	struct le *le;
	struct sa dst;
	int err;

	if (!net)
		return NULL;

	sa_init(&dst, af);
	if (af == AF_INET6)
		err = sa_set_str(&dst, "1::1", 53);
	else
		err = sa_set_str(&dst, "1.1.1.1", 53);

	if (err)
		return NULL;

	LIST_FOREACH(&net->laddrs, le) {
		struct laddr *laddr = le->data;
		if (sa_af(&laddr->sa) != af)
			continue;

		if ((lc & LADDR_NOLINKLOCAL) && sa_is_linklocal(&laddr->sa))
			continue;

		if ((lc & LADDR_INTERNET) && check_route(&laddr->sa, &dst))
			continue;

		return &laddr->sa;
	}

	return NULL;
}


static bool print_addr(const char *ifname, const struct sa *sa, void *arg)
{
	(void) arg;

	info(" %10s:  %j\n", ifname, sa);
	return false;
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
	int err;

	if (!netp || !cfg)
		return EINVAL;

	/*
	 * baresip/libre must be built with matching HAVE_INET6 value.
	 * if different the size of `struct sa' will not match and the
	 * application is very likely to crash.
	 */
	if (!check_ipv6()) {
		warning("libre was compiled without IPv6-support"
			", but baresip was compiled with\n");
		return EAFNOSUPPORT;
	}

	net = mem_zalloc(sizeof(*net), net_destructor);
	if (!net)
		return ENOMEM;

	net->cfg = *cfg;

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

	if (cfg->use_getaddrinfo)
		dnsc_getaddrinfo(net->dnsc, true);
	else
		dnsc_getaddrinfo(net->dnsc, false);

	net_if_apply(add_laddr_filter, net);
	info("Local network addresses:\n");
	if (!list_count(&net->laddrs))
		info("  None available for net_interface: %s\n",
				str_isset(cfg->ifname) ? cfg->ifname : "-");
	else
		net_laddr_apply(net, print_addr, NULL);

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

	net->nsn = (uint32_t)min(RE_ARRAY_SIZE(net->nsv), srvc);

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
	struct config_net *cfg = &net->cfg;

	if (!net)
		return EINVAL;

	re_snprintf(cfg->ifname, sizeof(cfg->ifname), "%j", ip);

	net_flush_addresses(net);
	net_if_apply(add_laddr_filter, net);
	return 0;
}


/**
 * Add a local IP address
 *
 * @param net  Network instance
 * @param ip   IP address
 *
 * @return 0 if success, otherwise errorcode
 */
int net_add_address(struct network *net, const struct sa *ip)
{
	char ifname[256] = "???";
	int err;

	if (!net || !sa_isset(ip, SA_ADDR))
		return EINVAL;

	err = net_if_getname(ifname, sizeof(ifname), sa_af(ip), ip);
	if (err)
		goto out;

	err = net_add_address_ifname(net, ip, ifname);

out:
	return err;
}


/**
 * Remove a local IP address
 *
 * @param net  Network instance
 * @param ip   IP address
 *
 * @return 0 if success, otherwise errorcode
 */
int net_rm_address(struct network *net, const struct sa *ip)
{
	struct le *le;
	if (!net)
		return EINVAL;

	LIST_FOREACH(&net->laddrs, le) {
		struct laddr *laddr = le->data;
		if (sa_cmp(&laddr->sa, ip, SA_ADDR)) {
			mem_deref(laddr);
			break;
		}
	}

	return 0;
}


/**
 * Remove all local IP addresses
 *
 * @param net  Network instance
 *
 * @return 0 if success, otherwise errorcode
 */
int net_flush_addresses(struct network *net)
{
	struct le *le;
	if (!net)
		return EINVAL;

	le = list_head(&net->laddrs);
	while (le) {
		struct laddr *laddr = le->data;
		le = le->next;

		mem_deref(laddr);
	}

	return 0;
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
	uint32_t i, nsn = RE_ARRAY_SIZE(nsv);
	bool from_sys = false;
	int err;

	if (!net)
		return 0;

	err = net_dns_srv_get(net, nsv, &nsn, &from_sys);
	if (err)
		nsn = 0;

	err = re_hprintf(pf, " DNS Servers from %s%s: (%u)\n",
			 from_sys ? "System" : "Config",
			 net->cfg.use_getaddrinfo ? "(+getaddrinfo)" : "",
			 nsn);
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


bool net_ifaddr_filter(const struct network *net, const char *ifname,
			      const struct sa *sa)
{
	const struct config_net *cfg = &net->cfg;
	struct sa ip;

	if (!sa_isset(sa, SA_ADDR))
		return false;

	if (sa_is_linklocal(sa) && !cfg->use_linklocal)
		return false;

	if (str_isset(cfg->ifname) && 0 == sa_set_str(&ip, cfg->ifname, 0) &&
			sa_cmp(&ip, sa, SA_ADDR))
		return true;

	if (str_isset(cfg->ifname) && str_cmp(cfg->ifname, ifname))
		return false;

	if (!net_af_enabled(net, sa_af(sa)))
		return false;

	if (sa_is_loopback(sa))
		return false;

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
const struct sa *net_laddr_af(const struct network *net, int af)
{
	const struct sa *sa;

	sa = find_laddr_af(net, af, LADDR_NOLINKLOCAL | LADDR_INTERNET);
	if (sa)
		return sa;

	sa = find_laddr_af(net, af, LADDR_NOLINKLOCAL);
	if (sa)
		return sa;

	sa = find_laddr_af(net, af, 0);
	return sa;
}


const struct sa *net_laddr_for(const struct network *net,
			       const struct sa *dst)
{
	struct le *le;

	if (!net || !sa_isset(dst, SA_ADDR))
		return NULL;

	LIST_FOREACH(&net->laddrs, le) {
		struct laddr *laddr = le->data;
		if (sa_af(&laddr->sa) != sa_af(dst))
			continue;

		if (check_route(&laddr->sa, dst))
			continue;

		return &laddr->sa;
	}

	return NULL;
}


static bool laddr_cmp(struct le *le, void *arg)
{
	struct laddr *laddr = le->data;
	struct sa *sa = arg;

	return sa_cmp(&laddr->sa, sa, SA_ADDR);
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
 * Checks if given IP address is a local address.
 *
 * @param net Network instance
 * @param sa  IP address to check
 *
 * @return true if sa is a local address, false if not
 */
bool net_is_laddr(const struct network *net, struct sa *sa)
{
	return NULL != list_apply(&net->laddrs, true, laddr_cmp, sa);
}


int net_set_dst_scopeid(const struct network *net, struct sa *dst)
{
	struct le *le;
	struct sa dstc;
	if (!net || !dst)
		return EINVAL;

	sa_cpy(&dstc, dst);
	LIST_FOREACH(&net->laddrs, le) {
		struct laddr *laddr = le->data;
		struct sa *sa = &laddr->sa;
		if (sa_af(sa) != AF_INET6 || !sa_is_linklocal(sa))
			continue;

		sa_set_scopeid(&dstc, sa_scopeid(&laddr->sa));
		if (!net_dst_is_source_addr(&dstc, &laddr->sa)) {
			sa_cpy(dst, &dstc);
			return 0;
		}
	}

	return ECONNREFUSED;
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


bool net_laddr_apply(const struct network *net, net_ifaddr_h *ifh, void *arg)
{
	struct le *le;
	if (!net || !ifh)
		return true;

	LIST_FOREACH(&net->laddrs, le) {
		struct laddr *laddr = le->data;
		if (ifh(laddr->ifname, &laddr->sa, arg))
			return true;
	}

	return false;
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
	err |= re_hprintf(pf, "enabled interfaces:\n");
	net_laddr_apply(net, if_debug_handler, argv);

	err |= net_dns_debug(pf, net);

	return err;
}
