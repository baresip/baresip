#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
#

MOD		:= speex
$(MOD)_SRCS	+= speex.c
$(MOD)_LFLAGS	+= -lspeex
$(MOD)_CFLAGS	+= -Wno-strict-prototypes

include mk/mod.mk
