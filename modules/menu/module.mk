#
# module.mk
#
# Copyright (C) 2010 Alfred E. Heggestad
#

MOD		:= menu
$(MOD)_SRCS	+= menu.c static_menu.c dynamic_menu.c

include mk/mod.mk
