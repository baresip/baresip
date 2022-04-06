#
# module.mk
#
# Copyright (C) 2021 Commend.com - h.ramoser@commend.com
#

MOD		:= pulse_async
$(MOD)_SRCS	+= pulse.c player.c recorder.c pastream.c

$(MOD)_LFLAGS	+= $(shell pkg-config --libs libpulse)
$(MOD)_CFLAGS	+= $(shell pkg-config --cflags libpulse)

include mk/mod.mk