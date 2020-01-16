#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
#

MOD		:= avformat
$(MOD)_SRCS	+= avformat.c
$(MOD)_LFLAGS	+= \
	`pkg-config --libs libavdevice libavformat libavcodec libavutil`

include mk/mod.mk
