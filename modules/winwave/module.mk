#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
#

MOD		:= winwave
$(MOD)_SRCS	+= winwave.c src.c play.c
$(MOD)_LFLAGS	+= -lwinmm

include mk/mod.mk
