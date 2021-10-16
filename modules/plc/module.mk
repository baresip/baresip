#
# module.mk
#
# Copyright (C) 2010 Alfred E. Heggestad
#

MOD		:= plc
$(MOD)_SRCS	+= plc.c
$(MOD)_LFLAGS	+= "-lspandsp"

include mk/mod.mk
