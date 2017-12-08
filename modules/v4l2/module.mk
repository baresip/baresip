#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
#

MOD		:= v4l2
$(MOD)_SRCS	+= v4l2.c
ifneq ($(HAVE_LIBV4L2),)
$(MOD)_LFLAGS	+= -lv4l2
$(MOD)_CFLAGS	+= -DHAVE_LIBV4L2
endif

include mk/mod.mk
