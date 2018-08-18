/**
 * @file avahi.c Avahi Zeroconf Module
 *
 * Copyright (C) 2010 Creytiv.com
 * Copyright (C) 2017 Jonathan Sieber
 */

/**
 * @defgroup avahi avahi
 *
 * This module implements DNS Service Discovery via Avahi Client API
 * It does 2 things:
 * 1) Announce _sipuri._udp resource for the main UA (under the local IP)
 * 2) Fills contact list with discovered hosts
 *
 * NOTE: This module is experimental.
 *
 */

#include <re.h>
#include <baresip.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <avahi-common/simple-watch.h>
#include <avahi-client/lookup.h>
#include <avahi-client/publish.h>
#include <avahi-common/error.h>

/* for if_nametoindex */
#include <net/if.h>

/* gethostname, getaddrinfo */
#define __USE_XOPEN2K
#include <unistd.h>
#include <netdb.h>


struct avahi_st {
	AvahiSimplePoll* poll;
	AvahiClient* client;
	AvahiEntryGroup* group;
	AvahiServiceBrowser* browser;
	struct ua* local_ua;
	struct tmr poll_timer;
};

static struct avahi_st* avahi = NULL;


static void group_callback(AvahiEntryGroup* group,
	AvahiEntryGroupState state, void* userdata)
{
	(void)group;
	(void)userdata;

	switch (state) {

	case AVAHI_ENTRY_GROUP_ESTABLISHED:
		info ("avahi: Service Registration completed\n");
		break;
	case AVAHI_ENTRY_GROUP_FAILURE:
	case AVAHI_ENTRY_GROUP_COLLISION:
		warning("avahi: Service Registration failed\n");
		/* TODO: Think of smart way to handle collision? */
		break;
	case AVAHI_ENTRY_GROUP_UNCOMMITED:
	case AVAHI_ENTRY_GROUP_REGISTERING:
		/* Do nothing */
		break;
	}
}


static void create_services(AvahiClient *client)
{
	int err;
	char buf[128] = "";
	const char *hostname;

	int if_idx = AVAHI_IF_UNSPEC;
	int af = AVAHI_PROTO_INET;

	struct sa laddr;

	/* Build announced sipuri as username@hostname */
	hostname = avahi_client_get_host_name_fqdn(client);

	re_snprintf(buf, sizeof(buf), "<sip:%s@%s>;regint=0",
				sys_username(),
				hostname);

	info("avahi: Creating local UA %s\n", buf);
	err = ua_alloc(&avahi->local_ua, buf);

	if (err) {
		warning("avahi: Could not create UA %s: %m\n", buf, err);
		return;
	}

	err = ua_register(avahi->local_ua);
	if (err) {
		warning("avahi: Could not register UA %s: %m\n", buf, err);
		return;
	}

	re_snprintf(buf, sizeof(buf), "sip:%s@%s",
				sys_username(),
				hostname);

	debug("avahi: Announcing URI: %s\n", buf);

	/* Get interface number of baresip interface */
	if (str_isset(conf_config()->net.ifname)) {
		if_idx = if_nametoindex(conf_config()->net.ifname);
	}

	if (net_af(baresip_network()) == AF_INET6) {
		af = AVAHI_PROTO_INET6;
	}

	err |= sip_transp_laddr(uag_sip(), &laddr, SIP_TRANSP_UDP, 0);
	if (err) {
		warning("avahi: Can not find local SIP address\n");
	}

	/* TODO: Query enabled transports and register these */
	avahi->group = avahi_entry_group_new(client, group_callback, NULL);
	err = avahi_entry_group_add_service(avahi->group,
		if_idx, af, 0,
		buf, "_sipuri._udp",
		NULL, NULL,
		ntohs(laddr.u.in.sin_port), NULL);
	err |= avahi_entry_group_commit(avahi->group);

	if (err) {
		warning("avahi: Error in registering service");
	}
}


static void client_callback(AvahiClient *c, AvahiClientState state,
			    AVAHI_GCC_UNUSED void * userdata)
{
	(void)c;

	switch (state) {

	case AVAHI_CLIENT_S_RUNNING:
		info("avahi: Avahi Daemon running\n", state);
		break;
	default:
		warning("avahi: unknown client_callback: %d\n", state);
		break;
	}
}


static void add_contact(const char* uri,
	const AvahiAddress *address, uint16_t port)
{
	int err;
	struct pl addr;
	char buf[128];
	struct contact *c;
	struct sa sa;
	struct sip_addr sipaddr;

	/* Parse SIPURI to get username and stuff... */
	pl_set_str(&addr, uri);
	if (sip_addr_decode(&sipaddr, &addr)) {
		warning("avahi: could not decode sipuri %s\n", uri);
		return;
	}

	if (address->proto == AVAHI_PROTO_INET6) {;
		sa_set_in6(&sa, address->data.ipv6.address, port);
	}
	else {
		sa_set_in(&sa, htonl(address->data.ipv4.address), port);
	}

	re_snprintf(buf, sizeof(buf),
		"\"%r@%r\" <sip:%r@%J>;presence=p2p",
		&sipaddr.uri.user, &sipaddr.uri.host,
		&sipaddr.uri.user, &sa);
	pl_set_str(&addr, buf);

	err = contact_add(baresip_contacts(), &c, &addr);
	if (err) {
		warning("Could not add contact %s: %m\n", buf, err);
	}
}


static void remove_contact_by_dname(const char* dname)
{
	const struct list *lst;
	struct le *le;

	/* remove sip: scheme for comparison */
	if (0 != re_regex(dname, str_len(dname), "^sip:")) {
		dname += 4;
	}

	lst = contact_list(baresip_contacts());

	for (le = list_head(lst); le; le = le->next) {
		struct contact *c = le->data;
		const struct sip_addr* addr = contact_addr(c);

		if (pl_strcmp(&addr->dname, dname) == 0) {
			contact_remove(baresip_contacts(), c);
			return;
		}
	}

	warning("avahi: Could not remove contact %s\n", dname);
}


static void resolve_callback(
	AvahiServiceResolver *r,
	AvahiIfIndex interface,
	AvahiProtocol protocol,
	AvahiResolverEvent event,
	const char *name,
	const char *type,
	const char *domain,
	const char *hostname,
	const AvahiAddress *address,
	uint16_t port,
	AvahiStringList *txt,
	AvahiLookupResultFlags flags,
	void *userdata)
{
	(void)r;
	(void)interface;
	(void)txt;
	(void)userdata;

	info("avahi: resolve %s %s %s %s\n", name, type, domain, hostname);

	if (event == AVAHI_RESOLVER_FOUND) {
		if (protocol != address->proto) {
			warning("avahi: Resolved address type ambiguous\n");
		}

		/* TODO: Process TXT field */
		if (!(flags & AVAHI_LOOKUP_RESULT_OUR_OWN)) {
			add_contact(name, address, port);
		}
	}
	else {
		warning("avahi: Resolver Error on %s: %s\n", name,
			avahi_strerror(avahi_client_errno(avahi->client)));
	}

	avahi_service_resolver_free(r);
}


static void browse_callback(
	AvahiServiceBrowser *b,
	AvahiIfIndex interface,
	AvahiProtocol protocol,
	AvahiBrowserEvent event,
	const char *name,
	const char *type,
	const char *domain,
	AVAHI_GCC_UNUSED AvahiLookupResultFlags flags,
	void* userdata)
{
	int proto = AVAHI_PROTO_INET;
	(void)b;
	(void)userdata;

	switch (event) {
	case AVAHI_BROWSER_NEW:
		debug("avahi: browse_callback if=%d proto=%d %s\n",
			interface, protocol, name);
		if (net_af(baresip_network()) == AF_INET6) {
			proto = AVAHI_PROTO_INET6;
		}

		if (!(avahi_service_resolver_new(avahi->client,
				interface, protocol,
				name, type, domain,
				proto, 0, resolve_callback,
				avahi->client))) {
			warning("avahi: Error resolving %s\n", name);
		}
	break;

	case AVAHI_BROWSER_REMOVE:
		remove_contact_by_dname(name);
		break;
	case AVAHI_BROWSER_ALL_FOR_NOW:
	case AVAHI_BROWSER_CACHE_EXHAUSTED:
		debug("avahi: (Browser) %s\n",
			event == AVAHI_BROWSER_CACHE_EXHAUSTED ?
			"CACHE_EXHAUSTED" : "ALL_FOR_NOW");
		break;
	default:
		warning("avahi: browse_callback %d %s\n", event, name);
		break;
	}
}


static void avahi_update(void* arg)
{
	(void)arg;

	avahi_simple_poll_iterate(avahi->poll, 0);
	tmr_start(&avahi->poll_timer, 250, avahi_update, 0);
}


static void destructor(void* arg)
{
	struct avahi_st* a = arg;

	tmr_cancel(&a->poll_timer);

	mem_deref(a->local_ua);

	/* Calling these destructor commands would be correct, but they
	 * spew out a lot of ugly D-Bus warning */
	if (a->browser) {
		avahi_service_browser_free(avahi->browser);
	}

	if (a->group) {
		avahi_entry_group_free(avahi->group);
	}

	if (a->client) {
		avahi_client_free(avahi->client);
	}
}


static int module_init(void)
{
	int err;
	avahi = mem_zalloc(sizeof(struct avahi_st), destructor);
	if (!avahi) {
		return ENOMEM;
	}

	avahi->poll = avahi_simple_poll_new();
	avahi->client = avahi_client_new(
		avahi_simple_poll_get(avahi->poll),
		0, client_callback, NULL, &err);

	/* Check wether creating the client object succeeded */
	if (!avahi->client) {
		warning("Failed to create client: %s\n", avahi_strerror(err));
		return err;
	}

	avahi->browser = avahi_service_browser_new(avahi->client,
		AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, "_sipuri._udp", NULL,
		0, browse_callback, 0);

	tmr_init(&avahi->poll_timer);
	avahi_update(0);

	/* Register services when UA is ready */
	if (!avahi->group) {
		create_services(avahi->client);
	}

	return 0;
}


static int module_close(void)
{
	debug("avahi: module_close\n");

	avahi = mem_deref(avahi);

	return 0;
}


const struct mod_export DECL_EXPORTS(avahi) = {
	"avahi",
	"application",
	module_init,
	module_close
};
