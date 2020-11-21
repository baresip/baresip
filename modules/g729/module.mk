#
# module.mk
#
# Copyright (C) 2020 Juha Heinanen
#

MOD		:= g729
$(MOD)_SRCS	+= g729.c

$(MOD)_LFLAGS	+= -lbcg729

include mk/mod.mk
