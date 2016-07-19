#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
#

MOD		:= avformat
$(MOD)_SRCS	+= avformat.c
$(MOD)_LFLAGS	+= -lavdevice -lavformat -lavcodec -lavutil
CFLAGS          += -DUSE_AVFORMAT

include mk/mod.mk
