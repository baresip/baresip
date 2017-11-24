#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
#

MOD		:= vp8
$(MOD)_SRCS	+= decode.c
$(MOD)_SRCS	+= encode.c
$(MOD)_SRCS	+= vp8.c
$(MOD)_SRCS	+= sdp.c
$(MOD)_LFLAGS	+= -lvpx

include mk/mod.mk
