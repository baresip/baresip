#
# module.mk
#
# Copyright (C) 2010 Alfred E. Heggestad
#

MOD		:= winwave
$(MOD)_SRCS	+= winwave.c src.c play.c
$(MOD)_LFLAGS	+= -lwinmm

include mk/mod.mk
