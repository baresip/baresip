#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
#

MOD		:= gsm
$(MOD)_SRCS	+= gsm.c
$(MOD)_LFLAGS	+= -L$(SYSROOT)/lib -lgsm
CFLAGS		+= -I$(SYSROOT)/include/gsm -I$(SYSROOT)/local/include

include mk/mod.mk
