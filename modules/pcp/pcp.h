/**
 * @file pcp.h Port Control Protocol module -- internal interface
 *
 * Copyright (C) 2010 - 2016 Creytiv.com
 */


/* listener */

struct pcp_listener;

typedef void (pcp_msg_h)(const struct pcp_msg *msg, void *arg);

int pcp_listen(struct pcp_listener **plp, const struct sa *srv,
	       pcp_msg_h *msgh, void *arg);
