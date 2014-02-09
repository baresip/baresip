#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
#

MOD		:= dtls_srtp
$(MOD)_SRCS	+= dtls_srtp.c dtls.c srtp.c tls_udp.c
$(MOD)_LFLAGS	+= -lsrtp

include mk/mod.mk
