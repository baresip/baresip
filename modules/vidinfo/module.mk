#
# module.mk
#
# Copyright (C) 2010 Alfred E. Heggestad
#

MOD		:= vidinfo
$(MOD)_SRCS	+= draw.c
$(MOD)_SRCS	+= vidinfo.c
$(MOD)_SRCS	+= xga_font_data.c

include mk/mod.mk
