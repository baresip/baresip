#
# module.mk
#
# Copyright (C) 2010 Alfred E. Heggestad
#

MOD		:= hisilicon
$(MOD)_SRCS	+= hisi.c hisi_src.c hisi_play.c
$(MOD)_LFLAGS	+=

include mk/mod.mk
