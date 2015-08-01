#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
#

MOD		:= gst_video
$(MOD)_SRCS	+= gst_video.c h264.c encode.c sdp.c
$(MOD)_LFLAGS	+= `pkg-config --libs gstreamer-0.10 gstreamer-app-0.10`
$(MOD)_CFLAGS   += `pkg-config --cflags gstreamer-0.10 gstreamer-app-0.10`

include mk/mod.mk
