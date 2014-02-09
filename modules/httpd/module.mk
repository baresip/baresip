#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
#

MOD		:= httpd
$(MOD)_SRCS	+= httpd.c

include mk/mod.mk
