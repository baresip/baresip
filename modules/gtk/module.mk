#
# module.mk - GTK+ Menu-based UI
#
# Copyright (C) 2010 Alfred E. Heggestad
# Copyright (C) 2015 Charles E. Lehner
#

MOD		:= gtk
$(MOD)_SRCS	+= gtk_mod.c call_window.c dial_dialog.c transfer_dialog.c \
	uri_entry.c
$(MOD)_LFLAGS	+= $(shell pkg-config --libs gtk+-3.0 $($(MOD)_EXTRA))
$(MOD)_CFLAGS	+= \
	$(shell pkg-config --cflags gtk+-3.0 $($(MOD)_EXTRA) | \
		sed -e 's/-I/-isystem/g' )
$(MOD)_CFLAGS	+= -Wno-strict-prototypes -Wno-deprecated-declarations

ifneq ($(USE_LIBNOTIFY),)
$(MOD)_EXTRA	 = libnotify
$(MOD)_CFLAGS	+= -DUSE_LIBNOTIFY=1
endif

include mk/mod.mk
