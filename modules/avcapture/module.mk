#
# module.mk
#
# Copyright (C) 2010 Alfred E. Heggestad
#

MOD		:= avcapture
$(MOD)_SRCS	+= avcapture.m
$(MOD)_LFLAGS	+= -framework AVFoundation

include mk/mod.mk
