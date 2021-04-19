#
# module.mk
#
# Copyright (C) 2010 Alfred E. Heggestad
#

MOD		:= snapshot
$(MOD)_SRCS	+= snapshot.c png_vf.c
ifeq ($(CC_NAME), gcc)
$(MOD)_CFLAGS	+= -Wno-clobbered
endif
$(MOD)_LFLAGS	+= -lpng

include mk/mod.mk
