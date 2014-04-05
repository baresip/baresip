#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
# Modified by Aaron Herting <aaron@herting.cc>
#

MOD		:= dtmfio
$(MOD)_SRCS	+= dtmfio.c
$(MOD)_LFLAGS	+=

include mk/mod.mk
