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

modules/$(MOD)/baresipbus.o :	modules/$(MOD)/baresipbus.h
modules/$(MOD)/ctrl_dbus.o :	modules/$(MOD)/baresipbus.h

modules/$(MOD)/baresipbus.h modules/$(MOD)/baresipbus.c: \
	modules/$(MOD)/com.github.Baresip.xml
	@echo "GEN $<"
	@gdbus-codegen --output-directory $(dir $@) \
		--generate-c-code baresipbus --c-namespace DBus \
		--interface-prefix com.github. $<

include mk/mod.mk
