#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
#

USE_X264 := $(shell [ -f $(SYSROOT)/include/x264.h ] || \
	[ -f $(SYSROOT)/local/include/x264.h ] || \
	[ -f $(SYSROOT_ALT)/include/x264.h ] && echo "yes")

MOD		:= avcodec
$(MOD)_SRCS	+= avcodec.c h263.c encode.c decode.c
$(MOD)_LFLAGS	+= -lavcodec -lavutil
CFLAGS          += -DUSE_AVCODEC
ifneq ($(USE_X264),)
CFLAGS          += -DUSE_X264
$(MOD)_LFLAGS	+= -lx264
endif
$(MOD)_CFLAGS	+= -isystem /usr/local/include

include mk/mod.mk
