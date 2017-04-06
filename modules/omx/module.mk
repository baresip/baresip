#
# module.mk
#
# Copyright (C) 2010 - 2015 Creytiv.com
#

MOD		:= omx
$(MOD)_SRCS	+= omx.c module.c

ifneq ($(USE_OMX_RPI),)
$(MOD)_CFLAGS	:= -DRASPBERRY_PI -DOMX_SKIP64BIT \
	-I/usr/local/include/interface/vmcs_host/linux/ \
	-I /usr/local/include/interface/vcos/pthreads/ \
	-I /opt/vc/include -I /opt/vc/include/interface/vmcs_host/linux \
	-I /opt/vc/include/interface/vcos/pthreads
$(MOD)_LFLAGS	+= -lvcos -lbcm_host -lopenmaxil -L /opt/vc/lib
endif

ifneq ($(USE_OMX_BELLAGIO),)
$(MOD)_LFLAGS  += -lomxil-bellagio
endif

include mk/mod.mk
