#
# module.mk
#
# Copyright (C) 2010 Alfred E. Heggestad
#

MOD		:= ilbc
$(MOD)_SRCS	+= ilbc.c
$(MOD)_LFLAGS	+= -lilbc -lm

include mk/mod.mk
