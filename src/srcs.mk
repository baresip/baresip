#
# srcs.mk All application source files.
#
# Copyright (C) 2010 Creytiv.com
#

SRCS	+= account.c
SRCS	+= aucodec.c
SRCS	+= audio.c
SRCS	+= aufilt.c
SRCS	+= auframe.c
SRCS	+= aulevel.c
SRCS	+= auplay.c
SRCS	+= ausrc.c
SRCS	+= baresip.c
SRCS	+= call.c
SRCS	+= cmd.c
SRCS	+= conf.c
SRCS	+= config.c
SRCS	+= contact.c
SRCS	+= custom_hdrs.c
SRCS	+= event.c
SRCS	+= h264.c
SRCS	+= log.c
SRCS	+= mctrl.c
SRCS	+= mediadev.c
SRCS	+= menc.c
SRCS	+= message.c
SRCS	+= metric.c
SRCS	+= mnat.c
SRCS	+= module.c
SRCS	+= net.c
SRCS	+= play.c
SRCS	+= reg.c
SRCS	+= rtpext.c
SRCS	+= rtpstat.c
SRCS	+= sdp.c
SRCS	+= sipreq.c
SRCS	+= stream.c
SRCS	+= stunuri.c
SRCS	+= timer.c
SRCS	+= timestamp.c
SRCS	+= ua.c
SRCS	+= ui.c
SRCS	+= vidcodec.c
SRCS	+= video.c
SRCS	+= vidfilt.c
SRCS	+= vidisp.c
SRCS	+= vidsrc.c
SRCS	+= vidutil.c

ifneq ($(STATIC),)
SRCS	+= static.c
endif

APP_SRCS += main.c
