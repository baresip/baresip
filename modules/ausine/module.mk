#
# module.mk
#
# Copyright (C) 2010 Alfred E. Heggestad
#

MOD		:= ausine
$(MOD)_SRCS	+= ausine.c
$(MOD)_LFLAGS	+=

include mk/mod.mk
