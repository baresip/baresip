#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
#

MOD		:= zrtp
$(MOD)_SRCS	+= zrtp.c
$(MOD)_LFLAGS	+= -lzrtp -lbn
$(MOD)_CFLAGS   += -isystem /usr/local/include/libzrtp
$(MOD)_CFLAGS   += -Wno-strict-prototypes

include mk/mod.mk
