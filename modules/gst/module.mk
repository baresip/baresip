#
# module.mk
#
# Copyright (C) 2010 Alfred E. Heggestad
#

MOD		:= gst
$(MOD)_SRCS	+= gst.c
$(MOD)_LFLAGS	+= $(shell pkg-config --libs gstreamer-1.0)
$(MOD)_CFLAGS	+= $(shell pkg-config --cflags gstreamer-1.0)
$(MOD)_CFLAGS	+= -Wno-cast-align

include mk/mod.mk
