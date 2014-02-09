#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
#

MOD		:= opengl
$(MOD)_SRCS	+= opengl.m
$(MOD)_LFLAGS	+= -framework OpenGL -framework Cocoa -lobjc

include mk/mod.mk
