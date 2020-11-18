#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
#

MOD		:= aufile
$(MOD)_SRCS	+= aufile.c
ifneq ($(USE_SNDFILE),)
$(MOD)_CFLAGS	+= -DUSE_SNDFILE=1
$(MOD)_LFLAGS	+= -lsndfile
$(MOD)_SRCS	+= aufile_play.c
endif
$(MOD)_LFLAGS	+=

include mk/mod.mk
