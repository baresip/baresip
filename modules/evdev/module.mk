#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
#

MOD		:= evdev
$(MOD)_SRCS	+= evdev.c
$(MOD)_SRCS	+= print.c
$(MOD)_LFLAGS	+=

include mk/mod.mk
