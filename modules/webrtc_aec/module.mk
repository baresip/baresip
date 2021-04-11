#
# module.mk
#
# Copyright (C) 2010 Alfred E. Heggestad
#


WEBRTC_PATH	:= ../webrtc_sdk


MOD		:= webrtc_aec

$(MOD)_SRCS	+= aec.cpp
$(MOD)_SRCS	+= encode.cpp
$(MOD)_SRCS	+= decode.cpp

CPPFLAGS	+= -isystem $(WEBRTC_PATH)/include

$(MOD)_LFLAGS	+= \
	-L$(WEBRTC_PATH)/lib/x64/Debug \
	-lwebrtc_full \
	-lstdc++


include mk/mod.mk
