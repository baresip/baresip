#
# module.mk
#
# Copyright (C) 2010 Alfred E. Heggestad
#

MOD		:= x11grab
$(MOD)_SRCS	+= x11grab.c
$(MOD)_LFLAGS	+= -lX11 -lXext
$(MOD)_CFLAGS	+= -Wno-variadic-macros

include mk/mod.mk
