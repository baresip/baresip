#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
#

MOD		:= selftest
$(MOD)_SRCS	+= selftest.c cmd.c ua.c
$(MOD)_SRCS	+= sip_server.c

include mk/mod.mk
