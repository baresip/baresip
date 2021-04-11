#
# module.mk
#
# Copyright (C) 2010 - 2015 Alfred E. Heggestad
#

MOD		:= v4l2_codec
$(MOD)_SRCS	+= v4l2_codec.c
$(MOD)_LFLAGS	+=

include mk/mod.mk
