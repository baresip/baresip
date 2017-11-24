#
# module.mk
#
# Copyright (C) 2010 - 2016 Creytiv.com
#

MOD		:= pulse
$(MOD)_SRCS	+= pulse.c
$(MOD)_SRCS	+= player.c
$(MOD)_SRCS	+= recorder.c
$(MOD)_LFLAGS	+= $(shell pkg-config --libs libpulse-simple)
$(MOD)_CFLAGS	+= $(shell pkg-config --cflags libpulse-simple)

include mk/mod.mk
