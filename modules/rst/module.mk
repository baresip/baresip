#
# module.mk
#
# Copyright (C) 2011 Creytiv.com
#

MOD		:= rst
$(MOD)_SRCS	+= audio.c
$(MOD)_SRCS	+= rst.c
$(MOD)_SRCS	+= video.c
$(MOD)_LFLAGS	+= $(shell pkg-config --libs cairo libmpg123)
$(MOD)_CFLAGS	+= $(shell pkg-config --cflags cairo libmpg123)

include mk/mod.mk
