#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
#

MOD		:= sdl2
$(MOD)_SRCS	+= sdl.c
$(MOD)_LFLAGS	+= -lSDL2

include mk/mod.mk
