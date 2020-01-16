#
# module.mk
#
# Copyright (C) 2019 Hessischer Rundfunk
#

MOD		:= aptx
$(MOD)_SRCS	+= decode.c
$(MOD)_SRCS	+= encode.c
$(MOD)_SRCS	+= aptx.c
$(MOD)_SRCS	+= sdp.c
$(MOD)_LFLAGS += -lopenaptx

include mk/mod.mk
