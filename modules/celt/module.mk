#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
#

MOD		:= celt
$(MOD)_SRCS	+= celt.c
$(MOD)_LFLAGS	+= `pkg-config --libs celt`

include mk/mod.mk
