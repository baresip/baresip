#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
#

MOD		:= v4l2
$(MOD)_SRCS	+= v4l2.c
ifeq ($(HAVE_LIBV4L2),yes)
$(MOD)_LFLAGS	+= -lv4l2
CFLAGS		+= -DHAVE_LIBV4L2
endif

include mk/mod.mk
