#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
#

MOD		:= g7221
$(MOD)_SRCS	+= decode.c
$(MOD)_SRCS	+= encode.c
$(MOD)_SRCS	+= g7221.c
$(MOD)_SRCS	+= sdp.c
$(MOD)_LFLAGS	+= -lg722_1

include mk/mod.mk
