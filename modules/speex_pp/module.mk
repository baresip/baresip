#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
#

MOD		:= speex_pp
$(MOD)_SRCS	+= speex_pp.c
ifneq ($(HAVE_SPEEXDSP),)
$(MOD)_LFLAGS	+= "-lspeexdsp"
else
$(MOD)_LFLAGS	+= "-lspeex"
endif

include mk/mod.mk
