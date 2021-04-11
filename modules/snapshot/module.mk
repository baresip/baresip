#
# module.mk
#
# Copyright (C) 2010 Alfred E. Heggestad
#

MOD		:= snapshot
$(MOD)_SRCS	+= snapshot.c png_vf.c
$(MOD)_CFLAGS	+= -Wno-clobbered
$(MOD)_LFLAGS	+= -lpng

include mk/mod.mk
