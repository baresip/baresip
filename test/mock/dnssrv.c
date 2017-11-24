/**
 * @file mock/dnssrv.c Mock DNS server
 *
 * Copyright (C) 2010 - 2016 Creytiv.com
 */
#include <string.h>
#include <re.h>
#include "../test.h"


#define DEBUG_MODULE "mock/dnssrv"
#define DEBUG_LEVEL 5
#include <re_dbg.h>


#define LOCAL_PORT 0


static void dns_server_match(struct dns_server *srv, struct list *rrl,
			     const char *name, uint16_t type)
{
	struct dnsrr *rr0 = NULL;
	struct le *le;

	le = srv->rrl.head;
	while (le) {

		struct dnsrr *rr = le->data;
		le = le->next;

		if (type == rr->type && 0==str_casecmp(name, rr->name)) {

			if (!rr0)
				rr0 = rr;
			list_append(rrl, &rr->le_priv, rr);
		}
	}

	/* If rotation is enabled, then rotate multiple entries
	   in a deterministic way (no randomness please) */
	if (srv->rotate && rr0) {
		list_unlink(&rr0->le);
		list_append(&srv->rrl, &rr0->le, rr0);
	}
}


static void decode_dns_query(struct dns_server *srv,
			     const struct sa *src, struct mbuf *mb)
{
	struct list rrl = LIST_INIT;
	struct dnshdr hdr;
	struct le *le;
	char *qname = NULL;
	size_t start, end;
	uint16_t type, dnsclass;
	int err = 0;

	start = mb->pos;
	end   = mb->end;

	if (dns_hdr_decode(mb, &hdr) || hdr.qr || hdr.nq != 1) {
		DEBUG_WARNING("unable to decode query header\n");
		return;
	}

	err = dns_dname_decode(mb, &qname, start);
	if (err) {
		DEBUG_WARNING("unable to decode query name\n");
		goto out;
	}

	if (mbuf_get_left(mb) < 4) {
		err = EBADMSG;
		DEBUG_WARNING("unable to decode query type/class\n");
		goto out;
	}

	type     = ntohs(mbuf_read_u16(mb));
	dnsclass = ntohs(mbuf_read_u16(mb));

	DEBUG_INFO("dnssrv: type=%s query-name='%s'\n",
		   dns_rr_typename(type), qname);

	if (dnsclass == DNS_CLASS_IN) {
		dns_server_match(srv, &rrl, qname, type);
	}

	hdr.qr    = true;
	hdr.tc    = false;
	hdr.rcode = DNS_RCODE_OK;
	hdr.nq    = 1;
	hdr.nans  = list_count(&rrl);

	mb->pos = start;

	err = dns_hdr_encode(mb, &hdr);
	if (err)
		goto out;

	mb->pos = end;

	DEBUG_INFO("dnssrv: @@ found %u answers for %s\n",
		   list_count(&rrl), qname);

	for (le = rrl.head; le; le = le->next) {
		struct dnsrr *rr = le->data;

		err = dns_rr_encode(mb, rr, 0, NULL, start);
		if (err)
			goto out;
	}

	mb->pos = start;

	(void)udp_send(srv->us, src, mb);

 out:
	list_clear(&rrl);
	mem_deref(qname);
}


static void udp_recv(const struct sa *src, struct mbuf *mb, void *arg)
{
	struct dns_server *srv = arg;

	decode_dns_query(srv, src, mb);
}


static void destructor(void *arg)
{
	struct dns_server *srv = arg;

	list_flush(&srv->rrl);
	mem_deref(srv->us);
}


int dns_server_alloc(struct dns_server **srvp, bool rotate)
{
	struct dns_server *srv;
	int err;

	if (!srvp)
		return EINVAL;

	srv = mem_zalloc(sizeof(*srv), destructor);
	if (!srv)
		return ENOMEM;

	err = sa_set_str(&srv->addr, "127.0.0.1", LOCAL_PORT);
	if (err)
		goto out;

	err = udp_listen(&srv->us, &srv->addr, udp_recv, srv);
	if (err)
		goto out;

	err = udp_local_get(srv->us, &srv->addr);
	if (err)
		goto out;

	srv->rotate = rotate;

 out:
	if (err)
		mem_deref(srv);
	else
		*srvp = srv;

	return err;
}


int dns_server_add_a(struct dns_server *srv, const char *name, uint32_t addr)
{
	struct dnsrr *rr;
	int err;

	if (!srv || !name)
		return EINVAL;

	rr = dns_rr_alloc();
	if (!rr)
		return ENOMEM;

	err = str_dup(&rr->name, name);
	if (err)
		goto out;

	rr->type = DNS_TYPE_A;
	rr->dnsclass = DNS_CLASS_IN;
	rr->ttl = 3600;
	rr->rdlen = 0;

	rr->rdata.a.addr = addr;

	list_append(&srv->rrl, &rr->le, rr);

 out:
	if (err)
		mem_deref(rr);

	return err;
}


int dns_server_add_srv(struct dns_server *srv, const char *name,
		       uint16_t pri, uint16_t weight, uint16_t port,
		       const char *target)
{
	struct dnsrr *rr;
	int err;

	if (!srv || !name || !port || !target)
		return EINVAL;

	rr = dns_rr_alloc();
	if (!rr)
		return ENOMEM;

	err = str_dup(&rr->name, name);
	if (err)
		goto out;

	rr->type = DNS_TYPE_SRV;
	rr->dnsclass = DNS_CLASS_IN;
	rr->ttl = 3600;
	rr->rdlen = 0;

	rr->rdata.srv.pri = pri;
	rr->rdata.srv.weight = weight;
	rr->rdata.srv.port = port;
	str_dup(&rr->rdata.srv.target, target);

	list_append(&srv->rrl, &rr->le, rr);

 out:
	if (err)
		mem_deref(rr);

	return err;
}
