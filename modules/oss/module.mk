#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
#

MOD		:= oss
$(MOD)_SRCS	+= oss.c
$(MOD)_LFLAGS	+=

ifeq ($(OS), openbsd)
$(MOD)_LFLAGS	+= -lossaudio
endif
ifeq ($(OS), netbsd)
$(MOD)_LFLAGS	+= -lossaudio
endif

include mk/mod.mk
