#
# module.mk
#
# Copyright (C) 2010 Alfred E. Heggestad
#

MOD		:= aubridge
$(MOD)_SRCS	+= aubridge.c device.c src.c play.c
$(MOD)_LFLAGS	+=

include mk/mod.mk
