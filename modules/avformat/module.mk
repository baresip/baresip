#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
#

MOD		:= avformat
$(MOD)_SRCS	+= avformat.c
$(MOD)_SRCS	+= audio.c
$(MOD)_SRCS	+= video.c
$(MOD)_LFLAGS	+= \
	`pkg-config --libs libavformat libavcodec libswresample libavutil`

include mk/mod.mk
