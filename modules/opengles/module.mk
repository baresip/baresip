#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
#

MOD		:= opengles
$(MOD)_SRCS	+= opengles.c

$(MOD)_CFLAGS	+= -DGL_GLEXT_PROTOTYPES

ifeq ($(OS),darwin)
$(MOD)_SRCS	+= context.m

$(MOD)_LFLAGS	+= -lobjc -framework CoreGraphics -framework CoreFoundation
endif

include mk/mod.mk
