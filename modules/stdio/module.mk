#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
#

MOD		:= stdio
$(MOD)_SRCS	+= stdio.c
$(MOD)_LFLAGS	+=

include mk/mod.mk
