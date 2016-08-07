/**
 * @file natbd.c NAT Behavior Discovery Module
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <re.h>
#include <baresip.h>


/**
 * @defgroup natbd natbd
 *
 * NAT Behavior Discovery Using STUN (RFC 5780)
 *
 * This module is only for diagnostics purposes and does not affect
 * the main SIP client. It uses the NATBD api in libre to detect the
 * NAT Behaviour, by sending STUN packets to a STUN server. Both
 * protocols UDP and TCP are supported.
 */


struct natbd {
	struct nat_hairpinning *nh;
	struct nat_filtering *nf;
	struct nat_lifetime *nl;
	struct nat_mapping *nm;
	struct nat_genalg *ga;
	struct stun_dns *dns;
	struct sa stun_srv;
	struct tmr tmr;
	char host[256];
	uint16_t port;
	uint32_t interval;
	bool terminated;
	int proto;
	int res_hp;
	enum nat_type res_nm;
	enum nat_type res_nf;
	struct nat_lifetime_interval res_nl;
	uint32_t n_nl;
	int status_ga;
};

static struct natbd *natbdv[2];


static const char *hairpinning_str(int res_hp)
{
	switch (res_hp) {

	case -1:  return "Unknown";
	case 0:   return "Not Supported";
	default:  return "Supported";
	}
}


static const char *genalg_str(int status)
{
	switch (status) {

	case -1: return "Not Detected";
	case 0:  return "Unknown";
	case 1:  return "Detected";
	default: return "???";
	}
}


static int natbd_status(struct re_printf *pf, void *arg)
{
	const struct natbd *natbd = arg;
	int err;

	if (!pf || !natbd)
		return 0;

	err  = re_hprintf(pf, "NAT Binding Discovery (using %s:%J)\n",
			  net_proto2name(natbd->proto),
			  &natbd->stun_srv);
	err |= re_hprintf(pf, "  Hairpinning: %s\n",
			  hairpinning_str(natbd->res_hp));
	err |= re_hprintf(pf, "  Mapping:     %s\n",
			  nat_type_str(natbd->res_nm));
	if (natbd->proto == IPPROTO_UDP) {
		err |= re_hprintf(pf, "  Filtering:   %s\n",
				  nat_type_str(natbd->res_nf));
		err |= re_hprintf(pf, "  Lifetime:    min=%u cur=%u max=%u"
				  " (%u probes)\n", natbd->res_nl.min,
				  natbd->res_nl.cur, natbd->res_nl.max,
				  natbd->n_nl);
	}
	err |= re_hprintf(pf, "  Generic ALG: %s\n",
			  genalg_str(natbd->status_ga));

	return err;
}


static void nat_hairpinning_handler(int err, bool supported, void *arg)
{
	struct natbd *natbd = arg;
	const int res_hp = (0 == err) ? supported : -1;

	if (natbd->terminated)
		return;

	if (res_hp != natbd->res_hp) {
		info("NAT Hairpinning %s changed from (%s) to (%s)\n",
		     net_proto2name(natbd->proto),
		     hairpinning_str(natbd->res_hp),
		     hairpinning_str(res_hp));
	}

	natbd->res_hp = res_hp;

	natbd->nh = mem_deref(natbd->nh);
}


static void nat_mapping_handler(int err, enum nat_type type, void *arg)
{
	struct natbd *natbd = arg;

	if (natbd->terminated)
		return;

	if (err) {
		warning("natbd: NAT mapping failed (%m)\n", err);
		goto out;
	}

	if (type != natbd->res_nm) {
		info("NAT Mapping %s changed from (%s) to (%s)\n",
		     net_proto2name(natbd->proto),
		     nat_type_str(natbd->res_nm),
		     nat_type_str(type));
	}

	natbd->res_nm = type;

 out:
	natbd->nm = mem_deref(natbd->nm);
}


static void nat_filtering_handler(int err, enum nat_type type, void *arg)
{
	struct natbd *natbd = arg;

	if (natbd->terminated)
		return;

	if (err) {
		warning("natbd: NAT filtering failed (%m)\n", err);
		goto out;
	}

	if (type != natbd->res_nf) {
		info("NAT Filtering %s changed from (%s) to (%s)\n",
		     net_proto2name(natbd->proto),
		     nat_type_str(natbd->res_nf),
		     nat_type_str(type));
	}

	natbd->res_nf = type;

 out:
	natbd->nf = mem_deref(natbd->nf);
}


static void nat_lifetime_handler(int err,
				 const struct nat_lifetime_interval *interval,
				 void *arg)
{
	struct natbd *natbd = arg;

	++natbd->n_nl;

	if (err) {
		warning("natbd: nat_lifetime_handler: (%m)\n", err);
		return;
	}

	natbd->res_nl = *interval;

	info("NAT Binding lifetime for %s: min=%u cur=%u max=%u\n",
	     net_proto2name(natbd->proto),
	     interval->min, interval->cur, interval->max);
}


static void nat_genalg_handler(int err, uint16_t scode, const char *reason,
			       int status, const struct sa *map,
			       void *arg)
{
	struct natbd *natbd = arg;

	(void)map;

	if (natbd->terminated)
		return;

	if (err) {
		warning("natbd: Generic ALG detection failed: %m\n", err);
		goto out;
	}
	else if (scode) {
		warning("natbd: Generic ALG detection failed: %u %s\n",
			scode, reason);
		goto out;
	}

	if (status != natbd->status_ga) {
		info("Generic ALG for %s changed from (%s) to (%s)\n",
		     net_proto2name(natbd->proto),
		     genalg_str(natbd->status_ga),
		     genalg_str(status));
	}

	natbd->status_ga = status;

 out:
	natbd->ga = mem_deref(natbd->ga);
}


static void destructor(void *arg)
{
	struct natbd *natbd = arg;

	natbd->terminated = true;

	tmr_cancel(&natbd->tmr);
	mem_deref(natbd->dns);
	mem_deref(natbd->nh);
	mem_deref(natbd->nm);
	mem_deref(natbd->nf);
	mem_deref(natbd->nl);
	mem_deref(natbd->ga);
}


static int natbd_start(struct natbd *natbd)
{
	struct network *net = baresip_network();
	int err = 0;

	if (!natbd->nh) {
		err |= nat_hairpinning_alloc(&natbd->nh, &natbd->stun_srv,
					     natbd->proto, NULL,
					     nat_hairpinning_handler, natbd);
		err |= nat_hairpinning_start(natbd->nh);
		if (err) {
			warning("natbd: nat_hairpinning_start() failed (%m)\n",
				err);
		}
	}

	if (!natbd->nm) {
		err |= nat_mapping_alloc(&natbd->nm,
					 net_laddr_af(net, net_af(net)),
					 &natbd->stun_srv, natbd->proto, NULL,
					 nat_mapping_handler, natbd);
		err |= nat_mapping_start(natbd->nm);
		if (err) {
			warning("natbd: nat_mapping_start() failed (%m)\n",
				err);
		}
	}

	if (natbd->proto == IPPROTO_UDP) {

		if (!natbd->nf) {
			err |= nat_filtering_alloc(&natbd->nf,
						   &natbd->stun_srv, NULL,
						   nat_filtering_handler,
						   natbd);
			err |= nat_filtering_start(natbd->nf);
			if (err) {
				warning("natbd: nat_filtering_start() (%m)\n",
					err);
			}
		}
	}

	if (!natbd->ga) {
		err |= nat_genalg_alloc(&natbd->ga, &natbd->stun_srv,
					natbd->proto, NULL,
					nat_genalg_handler, natbd);

		if (err) {
			warning("natbd: natbd_init: %m\n", err);
		}
		err |= nat_genalg_start(natbd->ga);
		if (err) {
			warning("natbd: nat_genalg_start() failed (%m)\n",
				err);
		}
	}

	return err;
}


static void timeout(void *arg)
{
	struct natbd *natbd = arg;

	info("%H\n", natbd_status, natbd);

	natbd_start(natbd);

	tmr_start(&natbd->tmr, natbd->interval * 1000, timeout, natbd);
}


static void dns_handler(int err, const struct sa *addr, void *arg)
{
	struct natbd *natbd = arg;

	if (err) {
		warning("natbd: failed to resolve '%s' (%m)\n",
			natbd->host, err);
		goto out;
	}

	info("natbd: resolved STUN-server for %s -- %J\n",
	     net_proto2name(natbd->proto), addr);

	sa_cpy(&natbd->stun_srv, addr);

	natbd_start(natbd);

	/* Lifetime discovery is a special test */
	if (natbd->proto == IPPROTO_UDP) {

		err  = nat_lifetime_alloc(&natbd->nl, &natbd->stun_srv, 3,
					  NULL, nat_lifetime_handler, natbd);
		err |= nat_lifetime_start(natbd->nl);
		if (err) {
			warning("natbd: nat_lifetime_start() failed (%m)\n",
				err);
		}
	}

	tmr_start(&natbd->tmr, natbd->interval * 1000, timeout, natbd);

 out:
	natbd->dns = mem_deref(natbd->dns);
}


static void timeout_init(void *arg)
{
	struct natbd *natbd = arg;
	const char *proto_str;
	int err = 0;

	if (sa_isset(&natbd->stun_srv, SA_ALL)) {
		dns_handler(0, &natbd->stun_srv, natbd);
		return;
	}

	if (natbd->proto == IPPROTO_UDP)
		proto_str = stun_proto_udp;
	else if (natbd->proto == IPPROTO_TCP)
		proto_str = stun_proto_tcp;
	else {
		err = EPROTONOSUPPORT;
		goto out;
	}

	err = stun_server_discover(&natbd->dns, net_dnsc(baresip_network()),
				   stun_usage_binding,
				   proto_str, net_af(baresip_network()),
				   natbd->host, natbd->port,
				   dns_handler, natbd);
	if (err)
		goto out;

 out:
	if (err) {
		warning("natbd: timeout_init: %m\n", err);
	}
}


static int natbd_alloc(struct natbd **natbdp, uint32_t interval,
		       int proto, const char *server)
{
	struct pl host, port;
	struct natbd *natbd;
	int err = 0;

	if (!natbdp || !interval || !proto || !server)
		return EINVAL;

	natbd = mem_zalloc(sizeof(*natbd), destructor);
	if (!natbd)
		return ENOMEM;

	natbd->interval = interval;
	natbd->proto = proto;
	natbd->res_hp = -1;

	if (0 == sa_decode(&natbd->stun_srv, server, str_len(server))) {
		;
	}
	else if (0 == re_regex(server, str_len(server), "[^:]+[:]*[^]*",
			       &host, NULL, &port)) {

		pl_strcpy(&host, natbd->host, sizeof(natbd->host));
		natbd->port = pl_u32(&port);
	}
	else {
		warning("natbd: failed to decode natbd_server (%s)\n",
			server);
		err = EINVAL;
		goto out;
	}

	tmr_start(&natbd->tmr, 1, timeout_init, natbd);

 out:
	if (err)
		mem_deref(natbd);
	else
		*natbdp = natbd;

	return err;
}


static int status(struct re_printf *pf, void *unused)
{
	size_t i;
	int err = 0;

	(void)unused;

	for (i=0; i<ARRAY_SIZE(natbdv); i++) {

		if (natbdv[i])
			err |= natbd_status(pf, natbdv[i]);
	}

	return err;
}


static const struct cmd cmdv[] = {
	{"natbd", 'z', 0, "NAT status", status}
};


static int module_init(void)
{
	char server[256] = "";
	uint32_t interval = 3600;
	int err;

	err = cmd_register(baresip_commands(), cmdv, ARRAY_SIZE(cmdv));
	if (err)
		return err;

	(void)conf_get_u32(conf_cur(), "natbd_interval", &interval);
	(void)conf_get_str(conf_cur(), "natbd_server", server, sizeof(server));

	if (!server[0]) {
		warning("natbd: missing config 'natbd_server'\n");
		return EINVAL;
	}

	info("natbd: Enable NAT Behavior Discovery using STUN server %s\n",
	     server);

	err |= natbd_alloc(&natbdv[0], interval, IPPROTO_UDP, server);
	err |= natbd_alloc(&natbdv[1], interval, IPPROTO_TCP, server);
	if (err) {
		warning("natbd: failed to allocate natbd state: %m\n", err);
	}

	return err;
}


static int module_close(void)
{
	size_t i;

	for (i=0; i<ARRAY_SIZE(natbdv); i++)
		natbdv[i] = mem_deref(natbdv[i]);

	cmd_unregister(baresip_commands(), cmdv);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(natbd) = {
	"natbd",
	"natbd",
	module_init,
	module_close,
};
