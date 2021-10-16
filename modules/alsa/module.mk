#
# module.mk
#
# Copyright (C) 2010 Alfred E. Heggestad
#

MOD		:= alsa
$(MOD)_SRCS	+= alsa.c alsa_src.c alsa_play.c
$(MOD)_LFLAGS	+= -lasound

include mk/mod.mk
