#
# module.mk
#
# Copyright (C) 2010 Alfred E. Heggestad
#

MOD		:= rtcpsummary
$(MOD)_SRCS	+= rtcpsummary.c
$(MOD)_LFLAGS	+=

include mk/mod.mk
