#
# module.mk
#
# Copyright (C) 2018 commend.com - Christoph Huber
#

MOD				:= onvif

# defines the media service version (Profile T needs media2!)

$(MOD)_SRCS += soap.c soap_str.c fault.c fakevideo.c rtspd.c onvif.c
$(MOD)_SRCS += device.c scopes.c wsd.c ptz.c event.c deviceio.c
$(MOD)_SRCS += filter.c pl.c
$(MOD)_SRCS += onvif_auth.c
$(MOD)_SRCS += media.c

include mk/mod.mk
