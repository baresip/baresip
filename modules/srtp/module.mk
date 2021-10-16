#
# module.mk
#
# Copyright (C) 2010 Alfred E. Heggestad
#

MOD		:= srtp
$(MOD)_SRCS	+= srtp.c sdes.c
$(MOD)_LFLAGS	+=

include mk/mod.mk
