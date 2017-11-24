#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
#

MOD		:= swscale
$(MOD)_SRCS	+= swscale.c
$(MOD)_LFLAGS	+= -lswscale

include mk/mod.mk
