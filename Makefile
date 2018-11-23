#
# Makefile
#
# Copyright (C) 2010 Creytiv.com
#
#
# Internal features:
#
#   USE_TLS           Enable SIP over TLS transport
#   USE_VIDEO         Enable Video-support
#

USE_VIDEO := 1

PROJECT	  := baresip
VERSION   := 0.6.0
DESCR     := "Baresip is a modular SIP User-Agent with audio and video support"

# Verbose and silent build modes
ifeq ($(V),)
HIDE=@
endif

ifndef LIBRE_MK
LIBRE_MK  := $(shell [ -f ../re/mk/re.mk ] && \
	echo "../re/mk/re.mk")
ifeq ($(LIBRE_MK),)
LIBRE_MK  := $(shell [ -f ../re-$(VERSION)/mk/re.mk ] && \
	echo "../re-$(VERSION)/mk/re.mk")
endif
ifeq ($(LIBRE_MK),)
LIBRE_MK  := $(shell [ -f /usr/share/re/re.mk ] && \
	echo "/usr/share/re/re.mk")
endif
ifeq ($(LIBRE_MK),)
LIBRE_MK  := $(shell [ -f /usr/local/share/re/re.mk ] && \
	echo "/usr/local/share/re/re.mk")
endif
endif

include $(LIBRE_MK)
include mk/modules.mk

ifndef LIBREM_PATH
LIBREM_PATH	:= $(shell [ -d ../rem ] && echo "../rem")
endif


CFLAGS    += -I. -Iinclude -I$(LIBRE_INC) -I$(SYSROOT)/include
CFLAGS    += -I$(LIBREM_PATH)/include
CFLAGS    += -I$(SYSROOT)/local/include/rem -I$(SYSROOT)/include/rem

CXXFLAGS  += -I. -Iinclude -I$(LIBRE_INC)
CXXFLAGS  += -I$(LIBREM_PATH)/include
CXXFLAGS  += -I$(SYSROOT)/local/include/rem -I$(SYSROOT)/include/rem
CXXFLAGS  += $(EXTRA_CXXFLAGS)

# XXX: common for C/C++
CPPFLAGS += -DHAVE_INTTYPES_H

ifneq ($(LIBREM_PATH),)
CLANG_OPTIONS  += -I$(LIBREM_PATH)/include
endif

ifeq ($(OS),win32)
STATIC    := yes
endif

ifneq ($(SYSROOT),)
ifeq ($(OS),freebsd)
CFLAGS += -I$(SYSROOT)/local/include
endif
ifeq ($(OS),openbsd)
CFLAGS += -isystem $(SYSROOT)/local/include
endif
endif


# Optional dependencies
ifneq ($(USE_VIDEO),)
CFLAGS    += -DUSE_VIDEO=1
endif
ifneq ($(STATIC),)
CFLAGS    += -DSTATIC=1
CXXFLAGS  += -DSTATIC=1
endif
CFLAGS    += -DMODULE_CONF

INSTALL := install
ifeq ($(DESTDIR),)
PREFIX  := /usr/local
else
PREFIX  := /usr
endif
BINDIR	:= $(PREFIX)/bin
INCDIR  := $(PREFIX)/include
BIN	:= $(PROJECT)$(BIN_SUFFIX)
TEST_BIN	:= selftest$(BIN_SUFFIX)
SHARED  := lib$(PROJECT)$(LIB_SUFFIX)
STATICLIB  := libbaresip.a
ifeq ($(STATIC),)
MOD_BINS:= $(patsubst %,%$(MOD_SUFFIX),$(MODULES))
endif
APP_MK	:= src/srcs.mk
TEST_MK	:= test/srcs.mk
MOD_MK	:= $(patsubst %,modules/%/module.mk,$(MODULES))
MOD_BLD	:= $(patsubst %,$(BUILD)/modules/%,$(MODULES))
LIBDIR     := $(PREFIX)/lib
MOD_PATH   := $(LIBDIR)/$(PROJECT)/modules
SHARE_PATH := $(PREFIX)/share/$(PROJECT)
CFLAGS     += -DPREFIX=\"$(PREFIX)\"
CFLAGS    += -DMOD_PATH=\"$(MOD_PATH)\"
CFLAGS    += -DSHARE_PATH=\"$(SHARE_PATH)\"


all: sanity $(MOD_BINS) $(BIN)

.PHONY: modules
modules:	$(MOD_BINS)

include $(APP_MK)
include $(TEST_MK)
include $(MOD_MK)

OBJS      := $(patsubst %.c,$(BUILD)/src/%.o,$(filter %.c,$(SRCS)))
OBJS      += $(patsubst %.m,$(BUILD)/src/%.o,$(filter %.m,$(SRCS)))
OBJS      += $(patsubst %.S,$(BUILD)/src/%.o,$(filter %.S,$(SRCS)))

APP_OBJS  := $(OBJS) $(patsubst %.c,$(BUILD)/src/%.o,$(APP_SRCS)) $(MOD_OBJS)

LIB_OBJS  := $(OBJS) $(MOD_OBJS)

TEST_OBJS := $(patsubst %.c,$(BUILD)/test/%.o,$(filter %.c,$(TEST_SRCS)))
TEST_OBJS += $(patsubst %.cpp,$(BUILD)/test/%.o,$(filter %.cpp,$(TEST_SRCS)))

ifneq ($(LIBREM_PATH),)
LIBS	+= -L$(LIBREM_PATH)
endif

# Static build: include module linker-flags in binary
ifneq ($(STATIC),)
LIBS      += $(MOD_LFLAGS)
else
LIBS      += -L$(SYSROOT)/local/lib
MOD_LFLAGS += -L$(SYSROOT)/local/lib
endif

LIBS      += -lrem -lm
LIBS      += -L$(SYSROOT)/lib

ifeq ($(OS),win32)
TEST_LIBS += -static-libgcc
endif


-include $(APP_OBJS:.o=.d)

-include $(TEST_OBJS:.o=.d)


sanity:
ifeq ($(LIBRE_MK),)
	@echo "ERROR: Missing common makefile for libre. Check LIBRE_MK"
	@exit 2
endif
ifeq ($(LIBRE_INC),)
	@echo "ERROR: Missing header files for libre. Check LIBRE_INC"
	@exit 2
endif
ifeq ($(LIBRE_SO),)
	@echo "ERROR: Missing library files for libre. Check LIBRE_SO"
	@exit 2
endif

Makefile:	mk/*.mk $(MOD_MK) $(LIBRE_MK)


$(SHARED): $(LIB_OBJS)
	@echo "  LD      $@"
	$(HIDE)$(LD) $(LFLAGS) $(SH_LFLAGS) $^ -L$(LIBRE_SO) -lre $(LIBS) -o $@

$(STATICLIB): $(LIB_OBJS)
	@echo "  AR      $@"
	@rm -f $@; $(AR) $(AFLAGS) $@ $^
ifneq ($(RANLIB),)
	@echo "  RANLIB  $@"
	$(HIDE)$(RANLIB) $@
endif

libbaresip.pc:
	@echo 'prefix='$(PREFIX) > libbaresip.pc
	@echo 'exec_prefix=$${prefix}' >> libbaresip.pc
	@echo 'libdir=$${prefix}/lib' >> libbaresip.pc
	@echo 'includedir=$${prefix}/include' >> libbaresip.pc
	@echo '' >> libbaresip.pc
	@echo 'Name: libbaresip' >> libbaresip.pc
	@echo 'Description: $(DESCR)' >> libbaresip.pc
	@echo 'Version: '$(VERSION) >> libbaresip.pc
	@echo 'URL: http://www.creytiv.com/baresip.html' >> libbaresip.pc
	@echo 'Libs: -L$${libdir} -lbaresip' >> libbaresip.pc
	@echo 'Cflags: -I$${includedir}' >> libbaresip.pc

# GPROF requires static linking
$(BIN):	$(APP_OBJS)
	@echo "  LD      $@"
ifneq ($(GPROF),)
	$(HIDE)$(LD) $(LFLAGS) $(APP_LFLAGS) $^ ../re/libre.a $(LIBS) -o $@
else
	$(HIDE)$(LD) $(LFLAGS) $(APP_LFLAGS) $^ \
		-L$(LIBRE_SO) -lre $(LIBS) -o $@
endif


.PHONY: test
test:	$(TEST_BIN)
	./$(TEST_BIN)

$(TEST_BIN):	$(STATICLIB) $(TEST_OBJS)
	@echo "  LD      $@"
	$(HIDE)$(CXX) $(LFLAGS) $(TEST_OBJS) \
		-L$(LIBRE_SO) -L. \
		-l$(PROJECT) -lre $(LIBS) $(TEST_LIBS) -o $@

$(BUILD)/%.o: %.c $(BUILD) Makefile $(APP_MK)
	@echo "  CC      $@"
	$(HIDE)$(CC) $(CFLAGS) -c $< -o $@ $(DFLAGS)

$(BUILD)/%.o: %.cpp $(BUILD) Makefile $(APP_MK)
	@echo "  CXX     $@"
	$(HIDE)$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@ $(DFLAGS)

$(BUILD)/%.o: %.m $(BUILD) Makefile $(APP_MK)
	@echo "  OC      $@"
	$(HIDE)$(CC) $(CFLAGS) $(OBJCFLAGS) -c $< -o $@ $(DFLAGS)

$(BUILD)/%.o: %.S $(BUILD) Makefile $(APP_MK)
	@echo "  AS      $@"
	$(HIDE)$(CC) $(CFLAGS) -c $< -o $@ $(DFLAGS)

$(BUILD): Makefile
	@mkdir -p $(BUILD)/src $(MOD_BLD) $(BUILD)/test/mock $(BUILD)/test/sip
	@touch $@

install: $(BIN) $(MOD_BINS)
	@mkdir -p $(DESTDIR)$(BINDIR)
	$(INSTALL) -m 0755 $(BIN) $(DESTDIR)$(BINDIR)
	@mkdir -p $(DESTDIR)$(MOD_PATH)
	$(INSTALL) -m 0644 $(MOD_BINS) $(DESTDIR)$(MOD_PATH)
	@mkdir -p $(DESTDIR)$(SHARE_PATH)
	$(INSTALL) -m 0644 share/* $(DESTDIR)$(SHARE_PATH)

install-dev: install-shared install-static

install-shared: $(SHARED) libbaresip.pc
	@mkdir -p $(DESTDIR)$(INCDIR)
	$(INSTALL) -Cm 0644 include/baresip.h $(DESTDIR)$(INCDIR)
	@mkdir -p $(DESTDIR)$(LIBDIR) $(DESTDIR)$(LIBDIR)/pkgconfig
	$(INSTALL) -m 0644 $(SHARED) $(DESTDIR)$(LIBDIR)
	$(INSTALL) -m 0644 libbaresip.pc $(DESTDIR)$(LIBDIR)/pkgconfig

install-static: $(STATICLIB)
	@mkdir -p $(DESTDIR)$(INCDIR)
	$(INSTALL) -Cm 0644 include/baresip.h $(DESTDIR)$(INCDIR)
	@mkdir -p $(DESTDIR)$(LIBDIR)
	$(INSTALL) -m 0644 $(STATICLIB) $(DESTDIR)$(LIBDIR)

uninstall:
	@rm -f $(DESTDIR)$(PREFIX)/bin/$(BIN)
	@rm -rf $(DESTDIR)$(MOD_PATH)
	@rm -f $(DESTDIR)$(PREFIX)/lib/$(SHARED)
	@rm -f $(DESTDIR)$(PREFIX)/lib/$(STATICLIB)
	@rm -f $(DESTDIR)$(PREFIX)/lib/pkgconfig/libbaresip.pc

.PHONY: clean
clean:
	@rm -rf $(BIN) $(MOD_BINS) $(SHARED) $(BUILD) $(TEST_BIN) \
		$(STATICLIB) libbaresip.pc
	@rm -f *stamp \
	`find . -name "*.[od]"` \
	`find . -name "*~"` \
	`find . -name "\.\#*"`

.PHONY: ccheck
ccheck:
	@ccheck.pl > /dev/null

version:
	@perl -pi -e 's/BARESIP_VERSION.*/BARESIP_VERSION \"$(VERSION)"/' \
		include/baresip.h
	@perl -pi -e "s/PROJECT_NUMBER         = .*/\
PROJECT_NUMBER         = $(VERSION)/" \
		mk/Doxyfile
	@echo "updating version number to $(VERSION)"

src/static.c: $(BUILD) Makefile $(APP_MK) $(MOD_MK)
	@echo "  SH      $@"
	@echo "/* static.c - autogenerated by makefile */"  > $@
	@echo "#include <re_types.h>"  >> $@
	@echo "#include <re_mod.h>"  >> $@
	@echo ""  >> $@
	@for n in $(MODULES); do \
		echo "extern const struct mod_export exports_$${n};" >> $@ ; \
	done
	@echo ""  >> $@
	@echo "const struct mod_export *mod_table[] = {"  >> $@
	@for n in $(MODULES); do \
		echo "  &exports_$${n},"  >> $@  ; \
	done
	@echo "  NULL"  >> $@
	@echo "};"  >> $@
