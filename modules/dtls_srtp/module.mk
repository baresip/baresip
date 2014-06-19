#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
#

MOD		:= dtls_srtp
$(MOD)_SRCS	+= dtls_srtp.c srtp.c dtls.c
$(MOD)_LFLAGS	+=

include mk/mod.mk
