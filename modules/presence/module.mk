#
# module.mk
#
# Copyright (C) 2010 Alfred E. Heggestad
#

MOD		:= presence
$(MOD)_SRCS	+= presence.c subscriber.c notifier.c publisher.c
$(MOD)_LFLAGS	+=

include mk/mod.mk
