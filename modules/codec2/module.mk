#
# module.mk
#
# Copyright (C) 2010 Alfred E. Heggestad
#

MOD		:= codec2
$(MOD)_SRCS	+= codec2.c
$(MOD)_LFLAGS	+= -lcodec2

include mk/mod.mk
