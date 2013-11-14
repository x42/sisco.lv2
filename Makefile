#!/usr/bin/make -f

OPTIMIZATIONS ?= -msse -msse2 -mfpmath=sse -ffast-math -fomit-frame-pointer -O3 -fno-finite-math-only
PREFIX ?= /usr/local
CFLAGS ?= -g -Wall -Wno-unused-function
LIBDIR ?= lib

###############################################################################

LV2DIR ?= $(PREFIX)/$(LIBDIR)/lv2

BUILDDIR=build/

LV2NAME=sisco
LV2GUI=siscoUI
BUNDLE=sisco.lv2

UNAME=$(shell uname)
ifeq ($(UNAME),Darwin)
  LV2LDFLAGS=-dynamiclib
  LIB_EXT=.dylib
else
  LV2LDFLAGS=-Wl,-Bstatic -Wl,-Bdynamic
  LIB_EXT=.so
endif

# check for build-dependencies

ifeq ($(shell pkg-config --exists lv2 || echo no), no)
  $(error "LV2 SDK was not found")
endif

ifeq ($(shell pkg-config --atleast-version=1.4 lv2 || echo no), no)
  $(error "LV2 SDK needs to be version 1.4 or later")
endif

ifeq ($(shell pkg-config --exists glib-2.0 gtk+-2.0 cairo || echo no), no)
  $(error "This plugin requires glib-2.0, gtk+-2.0 and cairo")
endif


# add library dependent flags and libs
override CFLAGS +=-fPIC $(OPTIMIZATIONS)
override CFLAGS +=`pkg-config --cflags lv2`

LOADLIBES = -lm 
GTKCFLAGS = `pkg-config --cflags gtk+-2.0 cairo`
GTKLIBS   = `pkg-config --libs gtk+-2.0 cairo`

targets=$(BUILDDIR)$(LV2NAME)$(LIB_EXT) $(BUILDDIR)$(LV2GUI)$(LIB_EXT)

# build target definitions

default: all

all: $(BUILDDIR)manifest.ttl $(BUILDDIR)$(LV2NAME).ttl $(targets)

$(BUILDDIR)manifest.ttl: manifest.ttl.in
	@mkdir -p $(BUILDDIR)
	sed "s/@LV2NAME@/$(LV2NAME)/;s/@LV2GUI@/$(LV2GUI)/;s/@LIB_EXT@/$(LIB_EXT)/" \
	  manifest.ttl.in > $(BUILDDIR)manifest.ttl

$(BUILDDIR)$(LV2NAME).ttl: $(LV2NAME).ttl.in
	@mkdir -p $(BUILDDIR)
	cat $(LV2NAME).ttl.in > $(BUILDDIR)$(LV2NAME).ttl

$(BUILDDIR)$(LV2NAME)$(LIB_EXT): lv2.c uris.h
	@mkdir -p $(BUILDDIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c99 \
	  -o $(BUILDDIR)$(LV2NAME)$(LIB_EXT) lv2.c \
	  -shared $(LV2LDFLAGS) $(LDFLAGS) $(LOADLIBES)

$(BUILDDIR)$(LV2GUI)$(LIB_EXT): ui.c uris.h \
    zita-resampler/resampler.cc zita-resampler/resampler-table.cc
	@mkdir -p $(BUILDDIR)
	$(CXX) $(CPPFLAGS) $(CFLAGS) $(GTKCFLAGS) \
	  -o $(BUILDDIR)$(LV2GUI)$(LIB_EXT) ui.c \
		zita-resampler/resampler.cc  zita-resampler/resampler-table.cc \
		-shared $(LV2LDFLAGS) $(LDFLAGS) $(GTKLIBS)


# install/uninstall/clean target definitions

install: all
	install -d $(DESTDIR)$(LV2DIR)/$(BUNDLE)
	install -m755 $(targets) $(DESTDIR)$(LV2DIR)/$(BUNDLE)
	install -m644 $(BUILDDIR)manifest.ttl $(BUILDDIR)$(LV2NAME).ttl $(DESTDIR)$(LV2DIR)/$(BUNDLE)

uninstall:
	rm -f $(DESTDIR)$(LV2DIR)/$(BUNDLE)/manifest.ttl
	rm -f $(DESTDIR)$(LV2DIR)/$(BUNDLE)/$(LV2NAME).ttl
	rm -f $(DESTDIR)$(LV2DIR)/$(BUNDLE)/$(LV2NAME)$(LIB_EXT)
	rm -f $(DESTDIR)$(LV2DIR)/$(BUNDLE)/$(LV2GUI)$(LIB_EXT)
	-rmdir $(DESTDIR)$(LV2DIR)/$(BUNDLE)

clean:
	rm -f $(BUILDDIR)manifest.ttl $(BUILDDIR)$(LV2NAME).ttl \
	  $(BUILDDIR)$(LV2NAME)$(LIB_EXT) \
	  $(BUILDDIR)$(LV2GUI)$(LIB_EXT)
	rm -rf $(BUILDDIR)*.dSYM
	-test -d $(BUILDDIR) && rmdir $(BUILDDIR) || true

distclean: clean
	rm -f cscope.out cscope.files tags

.PHONY: clean all install uninstall distclean
