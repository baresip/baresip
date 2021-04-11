#
# module.mk
#
# Copyright (C) 2010 Alfred E. Heggestad
#

MOD		:= speex_pp
$(MOD)_SRCS	+= speex_pp.c
$(MOD)_LFLAGS	+= "-lspeexdsp"

include mk/mod.mk
