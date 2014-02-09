#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
#

MOD		:= x11grab
$(MOD)_SRCS	+= x11grab.c
$(MOD)_LFLAGS	+= -L$(SYSROOT)/X11/lib -lX11 -lXext

include mk/mod.mk
