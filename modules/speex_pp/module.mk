#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
#

MOD		:= speex_pp
$(MOD)_SRCS	+= speex_pp.c
$(MOD)_LFLAGS	+= "-lspeexdsp"

include mk/mod.mk
