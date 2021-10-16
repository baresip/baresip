#
# module.mk
#
# Copyright (C) 2010 Alfred E. Heggestad
#

MOD		:= vumeter
$(MOD)_SRCS	+= vumeter.c
$(MOD)_LFLAGS	+=

include mk/mod.mk
