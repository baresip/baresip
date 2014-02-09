#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
#

MOD		:= dshow
$(MOD)_SRCS	+= dshow.cpp
$(MOD)_LFLAGS	+= -lstrmiids -lole32 -loleaut32 -lstdc++

include mk/mod.mk
