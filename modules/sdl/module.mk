#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
#

MOD		:= sdl
$(MOD)_SRCS	+= sdl.c
$(MOD)_SRCS	+= util.c

$(MOD)_LFLAGS	+= -lSDL
ifeq ($(OS),darwin)
# note: APP_LFLAGS is needed, as main.o links to -lSDLmain
APP_LFLAGS	+= -lSDL -lSDLmain -lobjc \
	-framework CoreFoundation -framework Foundation -framework Cocoa
endif

include mk/mod.mk
