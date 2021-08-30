#
# module.mk
#
# Copyright (C) 2021 Commend.com - Christian Spielberger
#

MOD		:= netroam
$(MOD)_SRCS	+= netroam.c

ifeq ($(OS),linux)
$(MOD)_CFLAGS	+= -DADD_NETLINK
$(MOD)_SRCS	+= netlink.c
endif

include mk/mod.mk
