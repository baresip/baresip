#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
#

MOD		:= x11
$(MOD)_SRCS	+= x11.c
$(MOD)_LFLAGS	+= -L$(SYSROOT)/X11/lib -lX11 -lXext
$(MOD)_CFLAGS	+= -Wno-variadic-macros

include mk/mod.mk
