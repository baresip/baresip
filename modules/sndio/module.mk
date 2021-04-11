#
# module.mk
#
# Copyright (C) 2014 Alfred E. Heggestad
#

MOD		:= sndio
$(MOD)_SRCS	+= sndio.c
$(MOD)_LFLAGS	+= -lsndio

include mk/mod.mk
