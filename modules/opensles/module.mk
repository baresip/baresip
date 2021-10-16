#
# module.mk
#
# Copyright (C) 2010 Alfred E. Heggestad
#

MOD		:= opensles
$(MOD)_SRCS	+= opensles.c
$(MOD)_SRCS	+= player.c
$(MOD)_SRCS	+= recorder.c
$(MOD)_LFLAGS	+= -lOpenSLES

include mk/mod.mk
