#
# module.mk
#
# Copyright (C) 2010 Alfred E. Heggestad
#

MOD		:= natpmp
$(MOD)_SRCS	+= natpmp.c libnatpmp.c

include mk/mod.mk
