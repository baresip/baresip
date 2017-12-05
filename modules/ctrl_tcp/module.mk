#
# module.mk
#
# Copyright (C) 2017 José Luis Millán <jmillan@aliax.net>
#

MOD		:= ctrl_tcp
$(MOD)_SRCS	+= ctrl_tcp.c

include mk/mod.mk
