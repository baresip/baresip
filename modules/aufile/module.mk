#
# module.mk
#
# Copyright (C) 2010 Alfred E. Heggestad
#

MOD		:= aufile
$(MOD)_SRCS	+= aufile.c
$(MOD)_SRCS	+= aufile_play.c aufile_src.c
$(MOD)_LFLAGS	+=

include mk/mod.mk
