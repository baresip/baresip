/**
 * @file netlink.h
 *
 * Copyright (c) 2021 Commend.com - c.spielberger@commend.com
 */

int open_netlink(net_change_h *changeh, void *arg);
void close_netlink(void);

