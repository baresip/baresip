#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
#

MOD		:= quicktime
$(MOD)_SRCS	+= quicktime.c
$(MOD)_LFLAGS	+= -framework QuickTime -lswscale

include mk/mod.mk
