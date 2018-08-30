#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
#

MOD		:= snapshot
$(MOD)_SRCS	+= snapshot.c png_vf.c sendfilename.c
$(MOD)_LFLAGS	+= -lpng

include mk/mod.mk
