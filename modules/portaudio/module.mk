#
# module.mk
#
# Copyright (C) 2010 Alfred E. Heggestad
#

MOD		:= portaudio
$(MOD)_SRCS	+= portaudio.c
$(MOD)_LFLAGS	+= -lportaudio

include mk/mod.mk
