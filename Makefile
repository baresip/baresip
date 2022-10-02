#
# Makefile
#
# Copyright (C) 2010 Alfred E. Heggestad
#
#
# Internal features:
#
#   USE_TLS           Enable SIP over TLS transport
#

PROJECT	  := baresip
VERSION   := 2.9.0
DESCR     := "Baresip is a modular SIP User-Agent with audio and video support"

# Verbose and silent build modes
ifeq ($(V),)
HIDE=@
endif

LIBRE_MIN	:= 2.9.0
LIBREM_MIN	:= 2.9.0

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

ifeq ($(LIBRE_PKG_PATH),)
LIBRE_PKG_PATH  := $(shell [ -f ../re/libre.pc ] && echo "../re/")
endif

ifeq ($(LIBRE_PKG_PATH),)
LIBRE_PKG_PATH  := $(shell [ -f $(PKG_CONFIG_PATH)/libre.pc ] && \
	echo "$(PKG_CONFIG_PATH)")
endif

ifeq ($(LIBRE_PKG_PATH),)
ifeq ($(LIBRE_SO),)
LIBRE_SO	:= $(patsubst %/share/re/re.mk,%/lib,$(LIBRE_MK))
endif
LIBRE_PKG_PATH  := $(shell [ -f $(LIBRE_SO)/pkgconfig/libre.pc ] && \
	echo "$(LIBRE_SO)/pkgconfig")
endif


include mk/modules.mk

ifeq ($(SYSROOT_LOCAL),)
ifeq  ($(SYSROOT),/usr)
SYSROOT_LOCAL := $(shell [ -d /usr/local/include ] && echo "/usr/local")
endif
endif

ifndef LIBREM_PATH
LIBREM_PATH	:= $(shell [ -d ../rem ] && echo "../rem")
endif

ifeq ($(LIBREM_PATH),)
ifneq ($(SYSROOT_LOCAL),)
LIBREM_PATH	:= $(shell [ -f $(SYSROOT_LOCAL)/include/rem/rem.h ] && \
	echo "$(SYSROOT_LOCAL)")
endif
endif

ifeq ($(LIBREM_PATH),)
LIBREM_PATH	:= $(shell [ -f $(SYSROOT)/include/rem/rem.h ] && \
	echo "$(SYSROOT)")
endif

# Include path
LIBREM_INC := $(shell [ -f $(LIBREM_PATH)/include/rem.h ] && \
	echo "$(LIBREM_PATH)/include")
ifeq ($(LIBREM_INC),)
LIBREM_INC := $(shell [ -f $(LIBREM_PATH)/include/rem/rem.h ] && \
	echo "$(LIBREM_PATH)/include/rem")
endif
ifeq ($(LIBREM_INC),)
LIBREM_INC := $(shell [ -f /usr/local/include/rem/rem.h ] && \
	echo "/usr/local/include/rem")
endif
ifeq ($(LIBREM_INC),)
LIBREM_INC := $(shell [ -f /usr/include/rem/rem.h ] && echo "/usr/include/rem")
endif

# Library path
ifeq ($(LIBREM_SO),)
LIBREM_SO  := $(shell [ -f $(LIBREM_PATH)/librem.a ] && \
	echo "$(LIBREM_PATH)")
endif
ifeq ($(LIBREM_SO),)
LIBREM_SO :=$(shell [ -f $(LIBREM_PATH)/librem$(LIB_SUFFIX) ] && \
	echo "$(LIBREM_PATH)")
endif
ifeq ($(LIBREM_SO),)
LIBREM_SO := $(shell [ -f $(LIBREM_PATH)/lib/librem$(LIB_SUFFIX) ] && \
	echo "$(LIBREM_PATH)/lib")
endif
ifeq ($(LIBREM_SO),)
LIBREM_SO  := $(shell [ -f /usr/local/lib/librem$(LIB_SUFFIX) ] \
	&& echo "/usr/local/lib")
endif
ifeq ($(LIBREM_SO),)
LIBREM_SO  := $(shell [ -f /usr/lib/librem$(LIB_SUFFIX) ] && \
	echo "/usr/lib")
endif
ifeq ($(LIBREM_SO),)
LIBREM_SO  := $(shell [ -f /usr/lib64/librem$(LIB_SUFFIX) ] && \
	echo "/usr/lib64")
endif


ifeq ($(LIBREM_PKG_PATH),)
LIBREM_PKG_PATH  := $(shell [ -f ../rem/librem.pc ] && echo "../rem/")
endif

ifeq ($(LIBREM_PKG_PATH),)
LIBREM_PKG_PATH  := $(shell [ -f $(PKG_CONFIG_PATH)/librem.pc ] && \
	echo "$(PKG_CONFIG_PATH)")
endif

ifeq ($(LIBREM_PKG_PATH),)
LIBREM_PKG_PATH  := $(shell [ -f $(LIBREM_SO)/pkgconfig/librem.pc ] && \
	echo "$(LIBREM_SO)/pkgconfig")
endif


# Dependency Checks
ifneq ($(PKG_CONFIG),)
ifeq ($(findstring $(MAKECMDGOALS), clean distclean),)
LIBRE_PKG := $(shell PKG_CONFIG_PATH=$(LIBRE_PKG_PATH) \
	pkg-config --exists "libre >= $(LIBRE_MIN)" --print-errors && \
	echo "yes")

ifeq ($(LIBRE_PKG),)
$(error bad libre version, required version is ">= $(LIBRE_MIN)" \
	LIBRE_MK: "$(LIBRE_MK)" \
	LIBRE_PKG_PATH: "$(LIBRE_PKG_PATH)")
endif

LIBREM_PKG := $(shell PKG_CONFIG_PATH=$(LIBREM_PKG_PATH) \
	pkg-config --exists "librem >= $(LIBREM_MIN)" --print-errors && \
	echo "yes")

ifeq ($(LIBREM_PKG),)
$(error bad librem version, required version is ">= $(LIBREM_MIN)" \
	LIBREM_PKG_PATH: "$(LIBREM_PKG_PATH)")
endif
endif
endif

CFLAGS    += -I. -Iinclude -I$(LIBRE_INC)
CFLAGS    += -I$(LIBREM_INC)


CXXFLAGS  += -I. -Iinclude -I$(LIBRE_INC)
CXXFLAGS  += -I$(LIBREM_INC)
CXXFLAGS  += $(EXTRA_CXXFLAGS)


# XXX: common for C/C++
CPPFLAGS += -DHAVE_INTTYPES_H

CLANG_OPTIONS  += -I$(LIBREM_INC)

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
ifneq ($(STATIC),)
CFLAGS    += -DSTATIC=1
CXXFLAGS  += -DSTATIC=1
endif

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

LIBS	+= -L$(LIBREM_SO)

# Static build: include module linker-flags in binary
ifneq ($(STATIC),)
LIBS      += $(MOD_LFLAGS)
else

ifneq ($(SYSROOT_LOCAL),)
LIBS      += -L$(SYSROOT_LOCAL)/lib
MOD_LFLAGS += -L$(SYSROOT_LOCAL)/lib
endif

endif

LIBS      += -lrem -lm
#LIBS      += -L$(SYSROOT)/lib

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
	@echo 'URL: https://github.com/baresip/baresip' >> libbaresip.pc
	@echo 'Libs: -L$${libdir} -lbaresip' >> libbaresip.pc
	@echo 'Cflags: -I$${includedir}' >> libbaresip.pc

$(BIN):	$(APP_OBJS)
	@echo "  LD      $@"
	$(HIDE)$(LD) $(LFLAGS) $(APP_LFLAGS) $^ \
		-L$(LIBRE_SO) -lre $(LIBS) -o $@


#
# List of modules used by selftest
#
ifneq ($(STATIC),)
TEST_MODULES :=
else
TEST_MODULES := g711.so ausine.so fakevideo.so auconv.so dtls_srtp.so
TEST_MODULES += srtp.so aufile.so
endif

.PHONY: test
test:	$(TEST_BIN)
	./$(TEST_BIN)

$(TEST_BIN):	$(STATICLIB) $(TEST_OBJS) $(TEST_MODULES)
	@echo "  LD      $@"
	$(HIDE)$(LD) $(LFLAGS) $(APP_LFLAGS) $(TEST_OBJS) \
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
ifeq ($(STATIC),)
	@mkdir -p $(DESTDIR)$(MOD_PATH)
	$(INSTALL) -m 0644 $(MOD_BINS) $(DESTDIR)$(MOD_PATH)
endif
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
		$(STATICLIB) libbaresip.pc .cache/baresip
	@rm -f *stamp \
	`find . -name "*.[od]"` \
	`find . -name "*~"` \
	`find . -name "\.\#*"`

.PHONY: ccheck
ccheck:
	@test/ccheck.py $(MOD_CCHECK_OPT)

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

bareinfo: info
	@echo "  LIBREM_PATH:   $(LIBREM_PATH)"
	@echo "  LIBREM_INC:    $(LIBREM_INC)"
	@echo "  LIBREM_SO:     $(LIBREM_SO)"
