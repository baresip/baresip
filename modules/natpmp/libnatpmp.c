/**
 * @file libnatpmp.c NAT-PMP Client library
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <string.h>
#include <re.h>
#include <baresip.h>
#include "libnatpmp.h"


enum {
	NATPMP_DELAY   =  250,
	NATPMP_MAXTX   =    9,
};

struct natpmp_req {
	struct natpmp_req **npp;
	struct udp_sock *us;
	struct tmr tmr;
	struct mbuf *mb;
	struct sa srv;
	unsigned n;
	natpmp_resp_h *resph;
	void *arg;
};


static void completed(struct natpmp_req *np, int err,
		      const struct natpmp_resp *resp)
{
	natpmp_resp_h *resph = np->resph;
	void *arg = np->arg;

	tmr_cancel(&np->tmr);

	if (np->npp) {
		*np->npp = NULL;
		np->npp = NULL;
	}

	np->resph = NULL;

	/* must be destroyed before calling handler */
	mem_deref(np);

	if (resph)
		resph(err, resp, arg);
}


static void destructor(void *arg)
{
	struct natpmp_req *np = arg;

	tmr_cancel(&np->tmr);
	mem_deref(np->us);
	mem_deref(np->mb);
}


static void timeout(void *arg)
{
	struct natpmp_req *np = arg;
	int err;

	if (np->n > NATPMP_MAXTX) {
		completed(np, ETIMEDOUT, NULL);
		return;
	}

	tmr_start(&np->tmr, NATPMP_DELAY<<np->n, timeout, arg);

#if 1
	debug("natpmp: {n=%u} tx %u bytes\n", np->n, np->mb->end);
#endif

	np->n++;

	np->mb->pos = 0;
	err = udp_send(np->us, &np->srv, np->mb);
	if (err) {
		completed(np, err, NULL);
	}
}


static int resp_decode(struct natpmp_resp *resp, struct mbuf *mb)
{
	resp->vers   =       mbuf_read_u8(mb);
	resp->op     =       mbuf_read_u8(mb);
	resp->result = ntohs(mbuf_read_u16(mb));
	resp->epoch  = ntohl(mbuf_read_u32(mb));

	if (!(resp->op & 0x80))
		return EPROTO;
	resp->op &= ~0x80;

	switch (resp->op) {

	case NATPMP_OP_EXTERNAL:
		resp->u.ext_addr = ntohl(mbuf_read_u32(mb));
		break;

	case NATPMP_OP_MAPPING_UDP:
	case NATPMP_OP_MAPPING_TCP:
		resp->u.map.int_port = ntohs(mbuf_read_u16(mb));
		resp->u.map.ext_port = ntohs(mbuf_read_u16(mb));
		resp->u.map.lifetime = ntohl(mbuf_read_u32(mb));
		break;

	default:
		warning("natmap: unknown opcode %d\n", resp->op);
		return EBADMSG;
	}

	return 0;
}


static void udp_recv(const struct sa *src, struct mbuf *mb, void *arg)
{
	struct natpmp_req *np = arg;
	struct natpmp_resp resp;

	if (!sa_cmp(src, &np->srv, SA_ALL))
		return;

	if (resp_decode(&resp, mb))
		return;

	completed(np, 0, &resp);
}


static int natpmp_init(struct natpmp_req *np, const struct sa *srv,
		       uint8_t opcode, natpmp_resp_h *resph, void *arg)
{
	int err;

	if (!np || !srv)
		return EINVAL;

	/* a new UDP socket for each NAT-PMP request */
	err = udp_listen(&np->us, NULL, udp_recv, np);
	if (err)
		return err;

	np->srv   = *srv;
	np->resph = resph;
	np->arg   = arg;

	udp_connect(np->us, srv);

	np->mb = mbuf_alloc(512);
	if (!np->mb)
		return ENOMEM;

	err |= mbuf_write_u8(np->mb, NATPMP_VERSION);
	err |= mbuf_write_u8(np->mb, opcode);

	return err;
}


int natpmp_external_request(struct natpmp_req **npp, const struct sa *srv,
			    natpmp_resp_h *resph, void *arg)
{
	struct natpmp_req *np;
	int err;

	np = mem_zalloc(sizeof(*np), destructor);
	if (!np)
		return ENOMEM;

	err = natpmp_init(np, srv, NATPMP_OP_EXTERNAL, resph, arg);
	if (err)
		goto out;

	timeout(np);

 out:
	if (err)
		mem_deref(np);
	else if (npp) {
		np->npp = npp;
		*npp = np;
	}
	else {
		/* Destroy the transaction now */
		mem_deref(np);
	}

	return err;
}


int natpmp_mapping_request(struct natpmp_req **npp, const struct sa *srv,
			   uint16_t int_port, uint16_t ext_port,
			   uint32_t lifetime, natpmp_resp_h *resph, void *arg)
{
	struct natpmp_req *np;
	int err;

	np = mem_zalloc(sizeof(*np), destructor);
	if (!np)
		return ENOMEM;

	err = natpmp_init(np, srv, NATPMP_OP_MAPPING_UDP, resph, arg);
	if (err)
		goto out;

	err |= mbuf_write_u16(np->mb, 0x0000);
	err |= mbuf_write_u16(np->mb, htons(int_port));
	err |= mbuf_write_u16(np->mb, htons(ext_port));
	err |= mbuf_write_u32(np->mb, htonl(lifetime));
	if (err)
		goto out;

	timeout(np);

 out:
	if (err)
		mem_deref(np);
	else if (npp) {
		np->npp = npp;
		*npp = np;
	}
	else {
		/* Destroy the transaction now */
		mem_deref(np);
	}

	return err;
}
