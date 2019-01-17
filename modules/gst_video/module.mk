#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
#

MOD		:= gst_video
$(MOD)_SRCS	+= gst_video.c encode.c sdp.c
$(MOD)_LFLAGS	+= $(shell pkg-config --libs gstreamer-0.10 gstreamer-app-0.10)
$(MOD)_CFLAGS   += \
	$(shell pkg-config --cflags gstreamer-0.10 gstreamer-app-0.10 | \
	sed -e 's/-I/-isystem/g')

include mk/mod.mk
