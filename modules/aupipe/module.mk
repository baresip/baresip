#
# module.mk
#
# Copyright (C) 2020 Orion Labs, Inc.
#

MOD		:= aupipe
$(MOD)_SRCS	+= aupipe.c
$(MOD)_LFLAGS	+=

include mk/mod.mk
