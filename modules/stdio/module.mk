#
# module.mk
#
# Copyright (C) 2010 Alfred E. Heggestad
#

MOD		:= stdio
$(MOD)_SRCS	+= stdio.c
$(MOD)_LFLAGS	+=

include mk/mod.mk
