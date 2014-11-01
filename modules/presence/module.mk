#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
#

MOD		:= presence
$(MOD)_SRCS	+= presence.c subscriber.c notifier.c publisher.c
$(MOD)_LFLAGS	+=

include mk/mod.mk
