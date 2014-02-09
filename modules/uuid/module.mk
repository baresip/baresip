#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
#

MOD		:= uuid
$(MOD)_SRCS	+= uuid.c
ifneq ($(OS),darwin)
$(MOD)_LFLAGS	+= -luuid
endif

include mk/mod.mk
