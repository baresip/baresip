#
# module.mk
#
# Copyright (C) 2016 Symonics GmbH
#

MOD		:= mpa
$(MOD)_SRCS	+= mpa.c
$(MOD)_SRCS	+= decode.c
$(MOD)_SRCS	+= sdp.c
$(MOD)_SRCS	+= encode.c
$(MOD)_LFLAGS	+= -ltwolame -lmp3lame -lmpg123 -lspeexdsp -lm

include mk/mod.mk
