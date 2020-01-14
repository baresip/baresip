/**
 * @file tcp_netstring.c  TCP netstring framing
 *
 * Copyright (C) 2018 46 Labs LLC
 */

#include <math.h>
#include <string.h>

#include <re_types.h>
#include <re_fmt.h>
#include <re_mem.h>
#include <re_mbuf.h>
#include <re_tcp.h>
#include <re_net.h>

#include "tcp_netstring.h"
#include "netstring/netstring.h"


#define DEBUG_MODULE "tcp_netstring"
#define DEBUG_LEVEL 5
#include <re_dbg.h>


struct netstring {
	struct tcp_conn *tc;
	struct tcp_helper *th;
	struct mbuf *mb;
	netstring_frame_h *frameh;
	void *arg;

	uint64_t n_tx;
	uint64_t n_rx;
};


/* responsible for adding the netstring header
   - assumes that the sent MBUF contains a complete packet
 */
static bool netstring_send_handler(int *err, struct mbuf *mb, void *arg)
{
	struct netstring *netstring = arg;
	size_t num_len;
	char num_str[32];

	if (mb->pos < NETSTRING_HEADER_SIZE) {
		DEBUG_WARNING("send: not enough space for netstring header\n");
		*err = ENOMEM;
		return true;
	}

	if (mbuf_get_left(mb) > NETSTRING_MAX_SIZE) {
		DEBUG_WARNING("send: buffer exceeds max size\n");
		*err = EMSGSIZE;
		return true;
	}

	/* Build the netstring. */
	if (mbuf_get_left(mb) == 0) {
		mb->buf[0] = '0';
		mb->buf[1] = ':';
		mb->buf[2] = ',';

		mb->end += 3;

		return false;
	}

	re_snprintf(num_str, sizeof(num_str), "%zu", mbuf_get_left(mb));
	num_len = strlen(num_str);

	mb->pos = NETSTRING_HEADER_SIZE - (num_len + 1);
	mbuf_write_mem(mb, (uint8_t*) num_str, num_len);
	mb->pos = NETSTRING_HEADER_SIZE - (num_len + 1);
	mb->buf[mb->pos + num_len] = ':';
	mb->buf[mb->end] = ',';

	mb->end += 1;

	++netstring->n_tx;

	return false;
}


static bool netstring_recv_handler(int *errp, struct mbuf *mbx, bool *estab,
			      void *arg)
{
	struct netstring *netstring = arg;
	int err = 0;
	size_t pos = 0;
	(void)estab;

	/* handle re-assembly */
	if (!netstring->mb) {
		netstring->mb = mbuf_alloc(1024);
		if (!netstring->mb) {
			*errp = ENOMEM;
			return true;
		}
	}

	pos = netstring->mb->pos;

	netstring->mb->pos = netstring->mb->end;

	err = mbuf_write_mem(netstring->mb, mbuf_buf(mbx),
			mbuf_get_left(mbx));
	if (err) {
		netstring->mb->pos = pos;
		goto out;
	}

	netstring->mb->pos = pos;

	/* extract all NETSTRING-frames in the TCP-stream */
	for (;;) {

		size_t len;
		struct mbuf mb;

		if (mbuf_get_left(netstring->mb) < (3))
			break;

		mbuf_init(&mb);

		err = netstring_read(
				(char*)netstring->mb->buf + netstring->mb->pos,
				netstring->mb->end,
				(char**)&mb.buf, &len);
		if (err) {

			if (err == NETSTRING_ERROR_TOO_SHORT) {
				DEBUG_INFO("receive: %s\n",
					netstring_error_str(err));
			}

			else {
				DEBUG_WARNING("receive: %s\n",
					netstring_error_str(err));
				netstring->mb = mem_deref(netstring->mb);
			}

			return false;
		}

		mb.end = len;

		++netstring->n_rx;

		netstring->frameh(&mb, netstring->arg);

		netstring->mb->pos += netstring_buffer_size(len);

		if (netstring->mb->pos >= netstring->mb->end) {
			netstring->mb = mem_deref(netstring->mb);
			break;
		}

		continue;
	}

 out:
	if (err)
		*errp = err;

	return true;  /* always handled */
}


static void destructor(void *arg)
{
	struct netstring *netstring = arg;

	mem_deref(netstring->th);
	mem_deref(netstring->tc);
	mem_deref(netstring->mb);
}


int netstring_insert(struct netstring **netstringp, struct tcp_conn *tc,
		int layer, netstring_frame_h *frameh, void *arg)
{
	struct netstring *netstring;
	int err;

	if (!netstringp || !tc || !frameh)
		return EINVAL;

	netstring = mem_zalloc(sizeof(*netstring), destructor);
	if (!netstring)
		return ENOMEM;

	netstring->tc = mem_ref(tc);
	err = tcp_register_helper(&netstring->th, tc, layer, NULL,
				  netstring_send_handler,
				  netstring_recv_handler, netstring);
	if (err)
		goto out;

	netstring->frameh = frameh;
	netstring->arg = arg;

 out:
	if (err)
		mem_deref(netstring);
	else
		*netstringp = netstring;

	return err;
}


int netstring_debug(struct re_printf *pf, const struct netstring *netstring)
{
	if (!netstring)
		return 0;

	return re_hprintf(pf, "tx=%llu, rx=%llu",
			              netstring->n_tx, netstring->n_rx);
}
