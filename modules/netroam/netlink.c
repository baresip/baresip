/**
 * @file netlink.c Network roaming module netlink socket extension
 *
 * For immediate detection of network changes on linux
 *
 * Copyright (C) 2021 Commend.com - c.spielberger@commend.com
 */

#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include <asm/types.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <re.h>
#include <baresip.h>
#include "netlink.h"

struct netlink  {
	re_sock_t fd;
	struct re_fhs *fhs;
	net_change_h *changeh;
	void *arg;
};

static struct netlink d = { RE_BAD_SOCK, NULL, NULL, NULL };


static void netlink_handler(int flags, void *arg)
{
	struct netlink *n = arg;
	char buf[256];
	(void)flags;

	while (read(n->fd, &buf, sizeof(buf)) > 0);

	if (n->changeh)
		n->changeh(d.arg);
}


int open_netlink(net_change_h *changeh, void *arg)
{
	struct sockaddr_nl sa;
	int err;
	memset(&sa, 0, sizeof(sa));
	sa.nl_family = AF_NETLINK;
	sa.nl_groups = RTMGRP_LINK | RTMGRP_IPV4_IFADDR;
	d.fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
	if (d.fd < 0) {
		err = errno;
		warning("netroam: open of netlink socket failed (%m)\n", err);
		return err;
	}

	err = net_sockopt_blocking_set(d.fd, false);
	if (err) {
		warning("netroam: netlink non-blocking failed (%m)\n", err);
		(void)close(d.fd);
		return err;
	}

	if (bind(d.fd, (struct sockaddr *) &sa, sizeof(sa)) < 0) {
		err = errno;
		warning("netroam: bind to netlink socket failed (%m)\n", err);
		(void)close(d.fd);
		return err;
	}

	d.changeh = changeh;
	d.arg	  = arg;
	return fd_listen(&d.fhs, d.fd, FD_READ, netlink_handler, &d);
}


void close_netlink(void)
{
	d.changeh = NULL;
	d.arg     = NULL;

	d.fhs = fd_close(d.fhs);
	close(d.fd);
	d.fd = RE_BAD_SOCK;
}
