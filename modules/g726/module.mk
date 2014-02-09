#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
#

MOD		:= g726
$(MOD)_SRCS	+= g726.c
$(MOD)_LFLAGS	+= -lspandsp

include mk/mod.mk
