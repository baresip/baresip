#
# module.mk
#
# Copyright (C) 2010 Alfred E. Heggestad
#

MOD		:= mqtt
$(MOD)_SRCS	+= mqtt.c
$(MOD)_SRCS	+= publish.c
$(MOD)_SRCS	+= subscribe.c
$(MOD)_LFLAGS   += -lmosquitto
$(MOD)_CFLAGS   +=

include mk/mod.mk
