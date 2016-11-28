#
# module.mk
#
# Copyright (C) 2010 - 2016 Creytiv.com
#

MOD		:= av1
$(MOD)_SRCS	+= av1.c
$(MOD)_SRCS	+= decode.c
$(MOD)_SRCS	+= encode.c
$(MOD)_LFLAGS	+= -laom

include mk/mod.mk
