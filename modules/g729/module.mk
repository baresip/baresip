#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
#

MOD		:= g729
$(MOD)_SRCS	+= g729.c
$(MOD)_LFLAGS	+= -lbcg729

include mk/mod.mk