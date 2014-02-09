#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
#

MOD		:= opus
$(MOD)_SRCS	+= decode.c
$(MOD)_SRCS	+= encode.c
$(MOD)_SRCS	+= opus.c
$(MOD)_SRCS	+= sdp.c
$(MOD)_LFLAGS	+= -lopus -lm

include mk/mod.mk
