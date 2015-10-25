#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
#

MOD		:= vidinfo
$(MOD)_SRCS	+= vidinfo.c panel.c
$(MOD)_LFLAGS	+= -lcairo

include mk/mod.mk
