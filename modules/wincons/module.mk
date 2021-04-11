#
# module.mk
#
# Copyright (C) 2010 Alfred E. Heggestad
#

MOD		:= wincons
$(MOD)_SRCS	+= wincons.c
$(MOD)_LFLAGS	+=

include mk/mod.mk
