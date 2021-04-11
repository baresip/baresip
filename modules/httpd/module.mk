#
# module.mk
#
# Copyright (C) 2010 Alfred E. Heggestad
#

MOD		:= httpd
$(MOD)_SRCS	+= httpd.c

include mk/mod.mk
