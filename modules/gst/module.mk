#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
#

MOD		:= gst
$(MOD)_SRCS	+= gst.c dump.c
$(MOD)_LFLAGS	+= `pkg-config --libs gstreamer-0.10`
CFLAGS		+= `pkg-config --cflags gstreamer-0.10`

include mk/mod.mk
