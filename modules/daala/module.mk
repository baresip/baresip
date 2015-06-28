#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
#

MOD		:= daala
$(MOD)_SRCS	+= decode.c
$(MOD)_SRCS	+= encode.c
$(MOD)_SRCS	+= daala.c
$(MOD)_LFLAGS	+= -ldaalaenc -ldaaladec -ldaalabase

include mk/mod.mk
