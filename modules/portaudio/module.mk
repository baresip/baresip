#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
#

MOD		:= portaudio
$(MOD)_SRCS	+= portaudio.c
$(MOD)_LFLAGS	+= -lportaudio

include mk/mod.mk
