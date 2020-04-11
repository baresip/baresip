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
	`pkg-config --libs libavformat libavcodec libswresample \
		libavutil libavdevice libavfilter libswscale libpostproc`

include mk/mod.mk
