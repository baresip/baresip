#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
#

MOD		:= isac
$(MOD)_SRCS	+= isac.c
$(MOD)_LFLAGS	+= -lisac

include mk/mod.mk
