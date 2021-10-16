#
# module.mk
#
# Copyright (C) 2010 Alfred E. Heggestad
#

MOD		:= x11
$(MOD)_SRCS	+= x11.c
$(MOD)_LFLAGS	+= -lX11 -lXext
$(MOD)_CFLAGS	+= -Wno-variadic-macros

include mk/mod.mk
