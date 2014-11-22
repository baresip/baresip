#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
#

MOD		:= avformat
$(MOD)_SRCS	+= avf.c
$(MOD)_LFLAGS	+= -lavdevice -lavformat -lavcodec -lavutil -lswscale
CFLAGS          += -DUSE_AVFORMAT

include mk/mod.mk
