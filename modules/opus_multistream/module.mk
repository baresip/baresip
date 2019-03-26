#
# module.mk
#
# Copyright (C) 2019 Creytiv.com
#

MOD		:= opus_multistream
$(MOD)_SRCS	+= decode.c
$(MOD)_SRCS	+= encode.c
$(MOD)_SRCS	+= opus_multistream.c
$(MOD)_SRCS	+= sdp.c
$(MOD)_LFLAGS	+= -lopus -lm

include mk/mod.mk
