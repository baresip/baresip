#
# module.mk
#
# Copyright (C) 2014 Creytiv.com
#

MOD		:= sndio
$(MOD)_SRCS	+= sndio.c
$(MOD)_LFLAGS	+= -lsndio

include mk/mod.mk
