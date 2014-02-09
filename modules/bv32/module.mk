#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
#

MOD		:= bv32
$(MOD)_SRCS	+= bv32.c
$(MOD)_LFLAGS	+= -lbv32 -lm

include mk/mod.mk
