#
# module.mk
#
# Copyright (C) 2010 Alfred E. Heggestad
#

MOD		:= g722
$(MOD)_SRCS	+= g722.c
$(MOD)_LFLAGS	+= -lspandsp

include mk/mod.mk
