#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
#

MOD		:= cairo
$(MOD)_SRCS	+= cairo.c
$(MOD)_LFLAGS	+= -lcairo
CFLAGS		+= -I$(SYSROOT)/include/cairo

include mk/mod.mk
