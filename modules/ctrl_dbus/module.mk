#
# module.mk
#
# Copyright (C) 2020 commend.com - Christian Spielberger
#

MOD		:= ctrl_dbus
$(MOD)_SRCS	+= baresipbus.c ctrl_dbus.c

$(MOD)_LFLAGS	+= $(shell pkg-config --libs glib-2.0 gio-unix-2.0)
$(MOD)_CFLAGS	+= \
	$(shell pkg-config --cflags glib-2.0 gio-unix-2.0 | \
		sed -e 's/-I/-isystem/g' )
$(MOD)_CFLAGS	+= -Wno-unused-parameter -Wno-declaration-after-statement \
	-Wno-pedantic -Wno-shorten-64-to-32

$(MOD)_CCHECK_OPT	= -e baresipbus.h -e baresipbus.c

modules/ctrl_dbus/baresipbus.h modules/ctrl_dbus/baresipbus.c: \
	modules/ctrl_dbus/com.github.Baresip.xml
	@cd $(dir $@) && ./gen.sh

include mk/mod.mk
