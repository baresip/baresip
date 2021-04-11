#
# module.mk
#
# Copyright (C) 2010 Alfred E. Heggestad
#

MOD		:= selfview
$(MOD)_SRCS	+= selfview.c
$(MOD)_LFLAGS	+=

include mk/mod.mk
