#
# module.mk
#
# Copyright (C) 2010 - 2016 Alfred E. Heggestad
#

MOD		:= av1
$(MOD)_SRCS	+= av1.c
$(MOD)_SRCS	+= decode.c
$(MOD)_SRCS	+= encode.c
$(MOD)_CFLAGS	+= -Wno-aggressive-loop-optimizations
$(MOD)_LFLAGS	+= -laom

include mk/mod.mk
