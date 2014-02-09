#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
#

MOD		:= sndfile
$(MOD)_SRCS	+= sndfile.c
$(MOD)_LFLAGS	+= -lsndfile

include mk/mod.mk
