#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
#

MOD		:= silk
$(MOD)_SRCS	+= silk.c
$(MOD)_LFLAGS	+= -lSKP_SILK_SDK

include mk/mod.mk
