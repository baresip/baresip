#
# module.mk
#
# Copyright (C) 2020 commend.com - Christian Spielberger
#

MOD		:= ctrl_dbus
$(MOD)_SRCS	+= ctrl_dbus.c baresipbus.c

$(MOD)_LFLAGS	+= $(shell pkg-config --libs glib-2.0 gio-unix-2.0)
$(MOD)_CFLAGS	+= \
	$(shell pkg-config --cflags glib-2.0 gio-unix-2.0 | \
		sed -e 's/-I/-isystem/g' )

$(MOD)_CCHECK_OPT	= -e baresipbus.h -e baresipbus.c

include mk/mod.mk

