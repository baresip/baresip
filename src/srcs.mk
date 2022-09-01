#
# srcs.mk All application source files.
#
# Copyright (C) 2010 Alfred E. Heggestad
#

SRCS	+= account.c
SRCS	+= aucodec.c
SRCS	+= audio.c
SRCS	+= aufilt.c
SRCS	+= auplay.c
SRCS	+= ausrc.c
SRCS	+= baresip.c
SRCS	+= bundle.c
SRCS	+= call.c
SRCS	+= cmd.c
SRCS	+= conf.c
SRCS	+= config.c
SRCS	+= contact.c
SRCS	+= custom_hdrs.c
SRCS	+= descr.c
SRCS	+= dial_number.c
SRCS	+= event.c
SRCS	+= http.c
SRCS	+= log.c
SRCS	+= mediadev.c
SRCS	+= mediatrack.c
SRCS	+= menc.c
SRCS	+= message.c
SRCS	+= metric.c
SRCS	+= mnat.c
SRCS	+= module.c
SRCS	+= net.c
SRCS	+= peerconn.c
SRCS	+= play.c
SRCS	+= reg.c
SRCS	+= rtpext.c
SRCS	+= rtpstat.c
SRCS	+= sdp.c
SRCS	+= sipreq.c
SRCS	+= stream.c
SRCS	+= stunuri.c
SRCS	+= timestamp.c
SRCS	+= ua.c
SRCS	+= uag.c
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
