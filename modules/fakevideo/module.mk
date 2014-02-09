#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
#

MOD		:= fakevideo
$(MOD)_SRCS	+= fakevideo.c
$(MOD)_LFLAGS	+=

include mk/mod.mk
