#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
#


WEBRTC_PATH	:= /usr/include/webrtc_audio_processing

MOD		:= webrtc_aec

$(MOD)_SRCS	+= aec.cpp
$(MOD)_SRCS	+= encode.cpp
$(MOD)_SRCS	+= decode.cpp

CPPFLAGS	+= -isystem $(WEBRTC_PATH)

$(MOD)_LFLAGS	+= \
	-L$(WEBRTC_PATH)/lib/x64/Debug \
	-lwebrtc_audio_processing \
	-lstdc++


include mk/mod.mk
