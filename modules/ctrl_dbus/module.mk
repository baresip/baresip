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
	-Wno-pedantic -Wno-shorten-64-to-32 -Wno-atomic-implicit-seq-cst

$(MOD)_CCHECK_OPT	= -e baresipbus.h -e baresipbus.c

PANDOC		:= $(shell pandoc -v dot 2> /dev/null)

modules/$(MOD)/baresipbus.c :	modules/$(MOD)/baresipbus.h
modules/$(MOD)/ctrl_dbus.c :	modules/$(MOD)/baresipbus.h

modules/$(MOD)/baresipbus.h modules/$(MOD)/baresipbus.c: \
	modules/$(MOD)/com.github.Baresip.xml
	@echo "  GEN     $<"
	@gdbus-codegen --output-directory $(dir $@) \
		--generate-c-code baresipbus --c-namespace DBus \
		--interface-prefix com.github. $<

ifdef PANDOC
$(MOD).html:	modules/$(MOD)/com.github.Baresip.xml
	@echo "DOC $<"
	@gdbus-codegen --output-directory modules/ctrl_dbus \
		--generate-docbook doc $<
	@pandoc --from docbook --to html \
		--output modules/ctrl_dbus/ctrl_dbus.html \
		modules/ctrl_dbus/doc-com.github.Baresip.xml
	@rm modules/ctrl_dbus/doc-com.github.Baresip.xml
endif


include mk/mod.mk
