#
# module.mk - DirectFB video display module
#
# Copyright (C) 2010 Creytiv.com
# Copyright (C) 2013 Andreas Shimokawa <andi@fischlustig.de>.
#

MOD                := directfb
$(MOD)_SRCS        += directfb.c
$(MOD)_LFLAGS      += `pkg-config --libs directfb `
CFLAGS             += `pkg-config --cflags directfb `

include mk/mod.mk
