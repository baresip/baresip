#
# module.mk
#
# Copyright (C) 2010 Alfred E. Heggestad
#

MOD		:= vidbridge
$(MOD)_SRCS	+= vidbridge.c src.c disp.c
$(MOD)_LFLAGS	+=

include mk/mod.mk
