#
# module.mk
#
# Copyright (C) 2020 Mikhail Kurkov
#

MOD		:= avfilter
$(MOD)_SRCS	+= avfilter.c
$(MOD)_SRCS	+= filter.c
$(MOD)_SRCS	+= util.c
$(MOD)_CFLAGS	+= -static -fPIC
$(MOD)_LFLAGS	+= -lavfilter -lavutil

include mk/mod.mk
