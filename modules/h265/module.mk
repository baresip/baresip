#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
#

MOD		:= h265
$(MOD)_SRCS	+= decode.c
$(MOD)_SRCS	+= encode.c
$(MOD)_SRCS	+= fmt.c
$(MOD)_SRCS	+= h265.c
$(MOD)_LFLAGS	+= `pkg-config --libs libavcodec libavutil`

include mk/mod.mk
