#
# module.mk - DirectFB video display module
#
# Copyright (C) 2010 Alfred E. Heggestad
# Copyright (C) 2013 Andreas Shimokawa <andi@fischlustig.de>.
#

MOD                := directfb
$(MOD)_SRCS        += directfb.c
$(MOD)_LFLAGS      += $(shell pkg-config --libs directfb)
$(MOD)_CFLAGS      += $(shell pkg-config --cflags directfb \
			| sed -e 's/-I/-isystem/g')

include mk/mod.mk
