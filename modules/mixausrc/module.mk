#
# module.mk
#
# Copyright (C) 2019 commend.com - Christian Spielberger
#

MOD		:= mixausrc
$(MOD)_SRCS	+= mixausrc.c
$(MOD)_LFLAGS	+=

include mk/mod.mk
