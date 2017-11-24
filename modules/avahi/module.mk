#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
#

MOD		:= avahi
$(MOD)_SRCS	+= avahi.c
$(MOD)_LFLAGS   += $(shell pkg-config --libs avahi-client)
$(MOD)_CFLAGS   += $(shell pkg-config --cflags avahi-client)

include mk/mod.mk
