#
# modules.mk
#
# Copyright (C) 2010 - 2017 Alfred E. Heggestad
#
# External libraries:
#
#   USE_AAC           AAC audio codec
#   USE_ALSA          ALSA audio driver
#   USE_AMR           Adaptive Multi-Rate (AMR) audio codec
#   USE_APTX          aptX audio codec
#   USE_AUDIOUNIT     AudioUnit audio driver for OSX/iOS
#   USE_AVCAPTURE     AVFoundation video capture for OSX/iOS
#   USE_AVCODEC       avcodec video codec module
#   USE_AVFORMAT      avformat video source module
#   USE_CODEC2        CODEC2 low-bitrate speech audio codec
#   USE_CONS          Console input driver
#   USE_COREAUDIO     MacOSX Coreaudio audio driver
#   USE_DBUS          DBus control interface
#   USE_ECHO          Echo module
#   USE_EVDEV         Event Device module
#   USE_G711          G.711 audio codec
#   USE_G722          G.722 audio codec
#   USE_G722_1        G.722.1 audio codec
#   USE_G726          G.726 audio codec
#   USE_GST           Gstreamer audio module
#   USE_GTK           GTK+ user interface
#   USE_HTTPREQ       HTTP request module
#   USE_JACK          JACK Audio Connection Kit audio driver
#   USE_L16           L16 audio codec
#   USE_MPA           MPA audio codec
#   USE_MPG123        Use mpg123
#   USE_NETROAM       Network roaming
#   USE_OPUS          Opus audio codec
#   USE_OPUS_MS       Opus multistream audio codec
#   USE_PLC           Packet Loss Concealment
#   USE_PORTAUDIO     Portaudio audio driver
#   USE_PULSE         Pulseaudio audio driver
#   USE_PULSE_ASYNC   Pulseaudio async audio driver
#   USE_RTCPSUMMARY   RTCP summary output after calls
#   USE_SDL           libSDL2 video output
#   USE_SNAPSHOT      Snapshot video module
#   USE_SNDFILE       sndfile wav dumper
#   USE_SNDIO         sndio audo driver
#   USE_SRTP          Secure RTP module using libre
#   USE_STDIO         stdio input driver
#   USE_SYSLOG        Syslog module
#   USE_V4L2          Video4Linux2 module
#   USE_WINWAVE       Windows audio driver
#   USE_X11           X11 video output
#


# Default is enabled
MOD_AUTODETECT := 1

ifneq ($(MOD_AUTODETECT),)

USE_CONS    := 1
USE_G711    := 1
USE_L16     := 1
USE_DBUS    := 1
USE_HTTPREQ := 1
USE_NETROAM := 1

ifneq ($(OS),win32)

USE_AAC       := $(shell $(call CC_TEST,fdk-aac/FDK_audio.h))
USE_ALSA      := $(shell $(call CC_TEST,alsa/asoundlib.h))
USE_AMR       := $(shell [ -d $(SYSROOT)/include/opencore-amrnb ] || \
	[ -d $(SYSROOT_LOCAL)/include/opencore-amrnb ] || \
	[ -d $(SYSROOT_ALT)/include/opencore-amrnb ] || \
	[ -d $(SYSROOT)/local/include/amrnb ] || \
	[ -d $(SYSROOT)/include/amrnb ] && echo "yes")
USE_APTX      := $(shell $(call CC_TEST,openaptx.h))
USE_AV1       := $(shell $(call CC_TEST,aom/aom.h))
USE_AVCODEC   := $(shell $(call CC_TEST,libavcodec/avcodec.h))
USE_AVFORMAT  := $(shell \
	$(call CC_TEST_AND,libavformat/avformat.h,libavdevice/avdevice.h))
USE_CODEC2    := $(shell $(call CC_TEST,codec2/codec2.h))
USE_DTLS      := $(shell $(call CC_TEST,openssl/dtls1.h))
USE_DTLS_SRTP := $(shell $(call CC_TEST,openssl/srtp.h))
USE_G722      := $(shell $(call CC_TEST,spandsp/g722.h))
USE_G722_1    := $(shell $(call CC_TEST,g722_1.h))
USE_G726      := $(shell $(call CC_TEST,spandsp/g726.h))
USE_GST       := $(shell pkg-config --exists gstreamer-1.0 && echo "yes")
USE_GTK       := $(shell pkg-config 'gtk+-3.0 >= 3.0' && \
		   pkg-config 'glib-2.0 >= 2.32' && echo "yes")
USE_JACK      := $(shell $(call CC_TEST,jack/jack.h))
USE_MPG123    := $(shell $(call CC_TEST,mpg123.h))
USE_OPUS      := $(shell $(call CC_TEST,opus/opus.h))
USE_OPUS_MS   := $(shell $(call CC_TEST,opus/opus_multistream.h))
USE_PLC       := $(shell $(call CC_TEST,spandsp/plc.h))
USE_PORTAUDIO := $(shell $(call CC_TEST,portaudio.h))
USE_PULSE     := $(shell pkg-config --exists libpulse && echo "yes")
USE_PULSE_ASYNC := $(shell pkg-config --exists libpulse && echo "yes")
USE_SDL       := $(shell $(call CC_TEST,SDL2/SDL.h))
USE_SNAPSHOT  := $(shell $(call CC_TEST,png.h))
USE_SNDFILE   := $(shell $(call CC_TEST,sndfile.h))
USE_SNDIO     := $(shell $(call CC_TEST,sndio.h))
USE_STDIO     := $(shell $(call CC_TEST,termios.h))
HAVE_GLIB     := $(shell pkg-config --exists "glib-2.0 >= 2.56" && echo "yes")
HAVE_SPEEXDSP := $(shell pkg-config --exists "speexdsp" && echo "yes")
ifneq ($(USE_MPG123),)
ifneq ($(HAVE_SPEEXDSP),)
USE_MPA  := $(shell $(call CC_TEST_AND,twolame.h,lame/lame.h))
endif
endif
USE_SWSCALE  := $(shell $(call CC_TEST,libswscale/swscale.h))
USE_SYSLOG   := $(shell $(call CC_TEST,syslog.h))
USE_MQTT     := $(shell $(call CC_TEST,mosquitto.h))
HAVE_LIBV4L2 := $(shell $(call CC_TEST,libv4l2.h))
USE_V4L2     := $(shell $(call CC_TEST,linux/videodev2.h))
ifeq ($(USE_V4L2),)
USE_V4L2     := $(shell $(call CC_TEST,sys/videoio.h))
endif
USE_X11      := $(shell $(call CC_TEST_AND,X11/Xlib.h,X11/extensions/XShm.h))
USE_ZRTP     := $(shell $(call CC_TEST,libzrtp/zrtp.h))
USE_VPX      := $(shell $(call CC_TEST,vpx/vp8.h))

USE_WEBRTC_AEC   := $(shell \
	pkg-config --exists "webrtc-audio-processing >= 0.3" && echo "yes")
else
# Windows.
# Accounts for mingw with Windows SDK (formerly known as Platform SDK)
# mounted at /winsdk
USE_DSHOW := $(shell [ -f /winsdk/Include/um/dshow.h ] && echo "yes")
endif

# Platform specific modules
ifeq ($(OS),darwin)

USE_AVFOUNDATION := \
	$(shell [ -d /System/Library/Frameworks/AVFoundation.framework ] \
		&& echo "yes")

USE_AUDIOUNIT := \
	$(shell [ -d /System/Library/Frameworks/AudioUnit.framework ] \
		&& echo "yes")

USE_COREAUDIO := \
	$(shell [ -d /System/Library/Frameworks/CoreAudio.framework ] \
		&& echo "yes")

ifneq ($(USE_AVFOUNDATION),)
USE_AVCAPTURE := yes
endif
endif # darwin

ifeq ($(OS),linux)
USE_EVDEV := $(shell $(call CC_TEST,linux/input.h))
endif

ifeq ($(OS),win32)
USE_WINWAVE := yes
MODULES   += wincons
endif

ifneq ($(USE_GTK),)
USE_LIBNOTIFY := $(shell pkg-config 'libnotify glib-2.0 < 2.40' && echo "yes")
endif

endif # MOD_AUTODETECT

# ------------------------------------------------------------------------- #

ifneq ($(BASIC_MODULES),no)
MODULES   += account
MODULES   += auconv
MODULES   += auresamp
MODULES   += contact
MODULES   += ctrl_tcp
MODULES   += debug_cmd
MODULES   += ebuacip
MODULES   += echo
MODULES   += fakevideo
MODULES   += httpd
MODULES   += ice
MODULES   += menu
MODULES   += mwi
MODULES   += natpmp
MODULES   += presence
MODULES   += rtcpsummary
MODULES   += selfview
MODULES   += serreg
MODULES   += srtp
MODULES   += stun
MODULES   += turn
MODULES   += uuid
MODULES   += vidbridge
MODULES   += vidinfo
MODULES   += vumeter
MODULES   += mixausrc
MODULES   += mixminus
MODULES   += multicast

ifneq ($(HAVE_PTHREAD),)
MODULES   += aubridge aufile ausine
endif

endif

ifneq ($(USE_AAC),)
MODULES   += aac
endif
ifneq ($(USE_ALSA),)
MODULES   += alsa
endif
ifneq ($(USE_AMR),)
MODULES   += amr
endif
ifneq ($(USE_APTX),)
MODULES   += aptx
endif
ifneq ($(USE_AV1),)
MODULES   += av1
endif
ifneq ($(USE_AUDIOUNIT),)
MODULES   += audiounit
endif
ifneq ($(USE_AVCAPTURE),)
MODULES   += avcapture
endif
ifneq ($(USE_AVCODEC),)
MODULES   += avcodec
ifneq ($(USE_AVFORMAT),)
MODULES   += avformat
endif
endif
ifneq ($(USE_AVFILTER),)
MODULES   += avfilter
endif
ifneq ($(USE_CODEC2),)
MODULES   += codec2
endif
ifneq ($(USE_CONS),)
MODULES   += cons
endif
ifneq ($(USE_COREAUDIO),)
MODULES   += coreaudio
endif
ifneq ($(USE_DTLS_SRTP),)
MODULES   += dtls_srtp
endif
ifneq ($(USE_ECHO),)
MODULES   += echo
endif
ifneq ($(USE_EVDEV),)
MODULES   += evdev
endif
ifneq ($(USE_G711),)
MODULES   += g711
endif
ifneq ($(USE_G722),)
MODULES   += g722
endif
ifneq ($(USE_G722_1),)
MODULES   += g7221
endif
ifneq ($(USE_G726),)
MODULES   += g726
endif
ifneq ($(HAVE_GLIB),)
ifneq ($(USE_DBUS),)
MODULES   += ctrl_dbus
endif
endif
ifneq ($(USE_GST),)
MODULES   += gst
endif
ifneq ($(USE_GTK),)
MODULES   += gtk
endif
ifneq ($(USE_HTTPREQ),)
MODULES   += httpreq
endif
ifneq ($(USE_JACK),)
MODULES   += jack
endif
ifneq ($(USE_L16),)
MODULES   += l16
endif
ifneq ($(USE_OPUS_MS),)
MODULES   += opus_multistream
endif
ifneq ($(USE_MPA),)
MODULES   += mpa
endif
ifneq ($(USE_MQTT),)
MODULES   += mqtt
endif
ifneq ($(USE_NETROAM),)
MODULES   += netroam
endif
ifneq ($(USE_OPUS),)
MODULES   += opus
endif
ifneq ($(USE_PLC),)
MODULES   += plc
endif
ifneq ($(USE_PORTAUDIO),)
MODULES   += portaudio
endif
ifneq ($(USE_PULSE),)
MODULES   += pulse
endif
ifneq ($(USE_PULSE_ASYNC),)
MODULES   += pulse_async
endif
ifneq ($(USE_SDL),)
MODULES   += sdl
endif
ifneq ($(USE_SNAPSHOT),)
MODULES   += snapshot
endif
ifneq ($(USE_SNDFILE),)
MODULES   += sndfile
endif
ifneq ($(USE_STDIO),)
MODULES   += stdio
endif
ifneq ($(USE_SNDIO),)
MODULES   += sndio
endif
ifneq ($(USE_SWSCALE),)
MODULES   += swscale
endif
ifneq ($(USE_SYSLOG),)
MODULES   += syslog
endif
ifneq ($(USE_V4L2),)
MODULES   += v4l2
endif
ifneq ($(USE_VPX),)
MODULES   += vp8
MODULES   += $(shell pkg-config 'vpx >= 1.3.0' && echo "vp9")
endif
ifneq ($(USE_WINWAVE),)
MODULES   += winwave
endif
ifneq ($(USE_X11),)
MODULES   += x11
endif
ifneq ($(USE_GZRTP),)
MODULES   += gzrtp
endif
ifneq ($(USE_DSHOW),)
MODULES   += dshow
endif
ifneq ($(USE_RTCPSUMMARY),)
MODULES   += rtcpsummary
endif
ifneq ($(USE_WEBRTC_AEC),)
MODULES   += webrtc_aec
endif

MODULES   += $(EXTRA_MODULES)
