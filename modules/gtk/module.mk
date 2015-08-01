#
# module.mk - GTK+ Menu-based UI
#
# Copyright (C) 2010 Creytiv.com
# Copyright (C) 2015 Charles E. Lehner
#

MOD		:= gtk
$(MOD)_SRCS	+= gtk_mod.c call_window.c dial_dialog.c transfer_dialog.c \
	uri_entry.c
$(MOD)_LFLAGS      += `pkg-config --libs gtk+-2.0 `
$(MOD)_CFLAGS      += `pkg-config --cflags gtk+-2.0 `
$(MOD)_CFLAGS	+= -Wno-strict-prototypes

include mk/mod.mk
