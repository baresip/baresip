#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
#

MOD		:= avcodec
$(MOD)_SRCS	+= avcodec.c h263.c encode.c decode.c
$(MOD)_LFLAGS	+= -lavcodec -lavutil
CFLAGS          += -DUSE_AVCODEC
$(MOD)_CFLAGS	+= -isystem /usr/local/include

include mk/mod.mk
