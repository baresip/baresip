#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
#

MOD		:= audiounit
$(MOD)_SRCS	+= audiounit.c
$(MOD)_SRCS	+= sess.c
$(MOD)_SRCS	+= player.c
$(MOD)_SRCS	+= recorder.c
$(MOD)_LFLAGS	+= -framework CoreAudio -framework AudioToolbox

include mk/mod.mk
