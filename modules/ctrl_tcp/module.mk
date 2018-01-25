#
# module.mk
#
# Copyright (C) 2018 46 Labs LLC
#

MOD		:= ctrl_tcp
$(MOD)_SRCS	+= ctrl_tcp.c tcp_netstring.c ./netstring/netstring.c

include mk/mod.mk
