/**
 * @file tcp_netstring.h  TCP netstring framing
 *
 * Copyright (C) 2018 46 Labs LLC
 */

enum {NETSTRING_HEADER_SIZE = 10};

struct netstring;

typedef bool (netstring_frame_h)(struct mbuf *mb, void *arg);


int netstring_insert(struct netstring **netstringp, struct tcp_conn *tc,
		int layer, netstring_frame_h *frameh, void *arg);
int netstring_debug(struct re_printf *pf, const struct netstring *netstring);
