#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
#

MOD		:= ilbc
$(MOD)_SRCS	+= ilbc.c
$(MOD)_LFLAGS	+= -lilbc -lm

include mk/mod.mk
