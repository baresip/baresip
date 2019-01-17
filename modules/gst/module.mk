#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
#

MOD		:= gst
$(MOD)_SRCS	+= gst.c dump.c
$(MOD)_LFLAGS	+= $(shell pkg-config --libs gstreamer-0.10)
$(MOD)_CFLAGS	+= $(shell pkg-config --cflags gstreamer-0.10 | \
	sed -e 's/-I/-isystem/g')

include mk/mod.mk
