#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
#

MOD		:= h265
$(MOD)_SRCS	+= h265.c encode.c decode.c fmt.c
$(MOD)_LFLAGS	+= `pkg-config --libs libavcodec libavutil`

include mk/mod.mk
