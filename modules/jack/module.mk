#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
#

MOD		:= jack
$(MOD)_SRCS	+= jack.c jack_play.c jack_src.c
$(MOD)_CFLAGS	+= $(shell pkg-config --cflags jack)
$(MOD)_LFLAGS	+= $(shell pkg-config --libs jack)

include mk/mod.mk
