#
# module.mk
#
# Copyright (C) 2010 Alfred E. Heggestad
#

MOD		:= syslog
$(MOD)_SRCS	+= syslog.c

include mk/mod.mk
