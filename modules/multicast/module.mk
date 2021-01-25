#
# module.mk
#
# Copyright (C) 2021 Commend.com - c.huber@commend.com
#

MOD		:= multicast

$(MOD)_SRCS	+= multicast.c sender.c receiver.c
$(MOD)_SRCS	+= player.c source.c

include mk/mod.mk
