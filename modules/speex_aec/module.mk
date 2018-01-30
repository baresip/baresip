#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
#

MOD		:= speex_aec
$(MOD)_SRCS	+= speex_aec.c
$(MOD)_LFLAGS	+= "-lspeexdsp"

include mk/mod.mk
