#
# module.mk
#
# Copyright (C) 2010 Alfred E. Heggestad
#

MOD		:= aac
$(MOD)_SRCS	+= decode.c
$(MOD)_SRCS	+= encode.c
$(MOD)_SRCS	+= aac.c
$(MOD)_SRCS	+= sdp.c
$(MOD)_LFLAGS	+= -lfdk-aac -lm
$(MOD)_CFLAGS	+= -Wno-unused-function

include mk/mod.mk
