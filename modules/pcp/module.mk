#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
#

MOD		:= pcp
$(MOD)_SRCS	+= pcp.c listener.c
$(MOD)_CFLAGS	+= -I$(SYSROOT)/local/include/rew
$(MOD)_LFLAGS	+= -lrew

include mk/mod.mk
