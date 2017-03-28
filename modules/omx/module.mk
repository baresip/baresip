#
# module.mk
#
# Copyright (C) 2010 - 2015 Creytiv.com
#

MOD		:= omx
$(MOD)_SRCS	+= omx.c module.c
$(MOD)_CFLAGS	:= -DOMX_SKIP64BIT \
	-I/usr/local/include/interface/vmcs_host/linux/ \
	-I /usr/local/include/interface/vcos/pthreads/ \
	-I /opt/vc/include -I /opt/vc/include/interface/vmcs_host/linux \
	-I /opt/vc/include/interface/vcos/pthreads
$(MOD)_LFLAGS	+= -lvcos -lbcm_host -lopenmaxil -L /opt/vc/lib

include mk/mod.mk
