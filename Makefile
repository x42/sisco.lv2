#!/usr/bin/make -f

OPTIMIZATIONS ?= -msse -msse2 -mfpmath=sse -ffast-math -fomit-frame-pointer -O3 -fno-finite-math-only
PREFIX ?= /usr/local
CFLAGS ?= -g -Wall -Wno-unused-function
LIBDIR ?= lib

EXTERNALUI?=yes
KXURI?=yes
RW?=robtk/
###############################################################################
LV2DIR ?= $(PREFIX)/$(LIBDIR)/lv2
bindir ?= $(PREFIX)/bin
mandir ?= $(PREFIX)/share/man
man1dir = $(mandir)/man1

BUILDDIR=build/
BUNDLE=sisco.lv2

LV2NAME=sisco
LV2GUI=siscoUI_gl
LV2GTK=siscoUI_gtk

sisco_VERSION?=$(shell git describe --tags HEAD | sed 's/-g.*$$//;s/^v//' || echo "LV2")
#########

LV2UIREQ=
LV2CFLAGS=$(CFLAGS) -I.
JACKCFLAGS=$(CFLAGS) -I.
GLUICFLAGS=-I.
GTKUICFLAGS=-I.

UNAME=$(shell uname)
ifeq ($(UNAME),Darwin)
  LV2LDFLAGS=-dynamiclib
  LIB_EXT=.dylib
  EXE_EXT=
  UI_TYPE=ui:CocoaUI
  PUGL_SRC=$(RW)pugl/pugl_osx.m
  PKG_GL_LIBS=
  GLUILIBS=-framework Cocoa -framework OpenGL -framework CoreFoundation
  BUILDGTK=no
else
  LV2LDFLAGS=-Wl,-Bstatic -Wl,-Bdynamic
  LIB_EXT=.so
  EXE_EXT=
  UI_TYPE=ui:X11UI
  PUGL_SRC=$(RW)pugl/pugl_x11.c
  PKG_GL_LIBS=glu gl
  GLUILIBS=-lX11
  GLUICFLAGS+=`pkg-config --cflags glu`
endif

ifneq ($(XWIN),)
  CC=$(XWIN)-gcc
  CXX=$(XWIN)-g++
  LV2LDFLAGS=-Wl,-Bstatic -Wl,-Bdynamic -Wl,--as-needed -lpthread
  LIB_EXT=.dll
  EXE_EXT=.exe
  PUGL_SRC=$(RW)pugl/pugl_win.cpp
  PKG_GL_LIBS=
  GLUILIBS=-lws2_32 -lwinmm -lopengl32 -lglu32 -lgdi32 -lcomdlg32 -lpthread
  BUILDGTK=no
  GLUICFLAGS=-I.
  override LDFLAGS += -static-libgcc -static-libstdc++
endif

ifeq ($(EXTERNALUI), yes)
  ifeq ($(KXURI), yes)
    UI_TYPE=kx:Widget
    LV2UIREQ+=lv2:requiredFeature kx:Widget;
    LV2CFLAGS += -DXTERNAL_UI
  else
    LV2UIREQ+=lv2:requiredFeature ui:external;
    LV2CFLAGS += -DXTERNAL_UI
    UI_TYPE=ui:external
  endif
endif

ifeq ($(BUILDOPENGL)$(BUILDGTK), nono)
  $(error at least one of gtk or openGL needs to be enabled)
endif

targets=$(BUILDDIR)$(LV2NAME)$(LIB_EXT)

ifneq ($(BUILDOPENGL), no)
targets+=$(BUILDDIR)$(LV2GUI)$(LIB_EXT)
endif

ifneq ($(BUILDGTK), no)
targets+=$(BUILDDIR)$(LV2GTK)$(LIB_EXT)
PKG_GTK_LIBS=glib-2.0 gtk+-2.0
else
PKG_GTK_LIBS=
endif

###############################################################################
# check for build-dependencies

ifeq ($(shell pkg-config --exists lv2 || echo no), no)
  $(error "LV2 SDK was not found")
endif

ifeq ($(shell pkg-config --atleast-version=1.4 lv2 || echo no), no)
  $(error "LV2 SDK needs to be version 1.4 or later")
endif

ifeq ($(shell pkg-config --exists pango cairo $(PKG_GTK_LIBS) $(PKG_GL_LIBS) || echo no), no)
  $(error "This plugin requires cairo pango $(PKG_GTK_LIBS) $(PKG_GL_LIBS)")
endif

ifeq ($(shell pkg-config --exists jack || echo no), no)
  $(warning *** libjack from http://jackaudio.org is required)
  $(error   Please install libjack-dev, libjack-jackd2-dev)
endif

ifneq ($(MAKECMDGOALS), submodules)
  ifeq ($(wildcard $(RW)robtk.mk),)
    $(warning This plugin needs https://github.com/x42/robtk)
    $(info set the RW environment variale to the location of the robtk headers)
    ifeq ($(wildcard .git),.git)
      $(info or run 'make submodules' to initialize robtk as git submodule)
    endif
    $(error robtk not found)
  endif
endif

# check for LV2 idle thread
ifeq ($(shell pkg-config --atleast-version=1.6.0 lv2 && echo yes), yes)
  GLUICFLAGS+=-DHAVE_IDLE_IFACE
  GTKUICFLAGS+=-DHAVE_IDLE_IFACE
  LV2UIREQ+=lv2:requiredFeature ui:idleInterface; lv2:extensionData ui:idleInterface;
endif

# check for lv2_atom_forge_object  new in 1.8.1 deprecates lv2_atom_forge_blank
ifeq ($(shell pkg-config --atleast-version=1.8.1 lv2 && echo yes), yes)
  override CFLAGS += -DHAVE_LV2_1_8
endif

# add library dependent flags and libs
LV2CFLAGS += `pkg-config --cflags lv2`
LV2CFLAGS += -fPIC $(OPTIMIZATIONS) -DVERSION="\"$(sisco_VERSION)\""

GTKUICFLAGS+= $(LV2CFLAGS) `pkg-config --cflags gtk+-2.0 cairo pango`
GTKUILIBS+=`pkg-config --libs gtk+-2.0 cairo pango`

GLUICFLAGS+= $(LV2CFLAGS) `pkg-config --cflags cairo pango`
GLUILIBS+=`pkg-config $(PKG_UI_FLAGS) --libs cairo pango pangocairo $(PKG_GL_LIBS)`

JACKCFLAGS+= $(OPTIMIZATIONS) -DVERSION="\"JACK $(sisco_VERSION)\"" -DDEFAULT_NOT_ONTOP
JACKCFLAGS+=`pkg-config --cflags jack lv2 pango pangocairo $(PKG_GL_LIBS)`
JACKLIBS=-lm `pkg-config $(PKG_UI_FLAGS) --libs jack pangocairo $(PKG_GL_LIBS)` $(GLUILIBS)

GLUICFLAGS+=-DUSE_GUI_THREAD
ifeq ($(GLTHREADSYNC), yes)
  GLUICFLAGS+=-DTHREADSYNC
endif

ROBGL+= Makefile
ROBGTK += Makefile


###############################################################################
# build target definitions
default: all

submodule_pull:
	-test -d .git -a .gitmodules -a -f Makefile.git && $(MAKE) -f Makefile.git submodule_pull

submodule_update:
	-test -d .git -a .gitmodules -a -f Makefile.git && $(MAKE) -f Makefile.git submodule_update

submodule_check:
	-test -d .git -a .gitmodules -a -f Makefile.git && $(MAKE) -f Makefile.git submodule_check

submodules:
	-test -d .git -a .gitmodules -a -f Makefile.git && $(MAKE) -f Makefile.git submodules

all: submodule_check $(BUILDDIR)manifest.ttl $(BUILDDIR)$(LV2NAME).ttl $(targets) $(BUILDDIR)x42-scope$(EXE_EXT)

$(BUILDDIR)manifest.ttl: lv2ttl/manifest.gl.ttl.in lv2ttl/manifest.gtk.ttl.in lv2ttl/manifest.lv2.ttl.in lv2ttl/manifest.ttl.in Makefile
	@mkdir -p $(BUILDDIR)
	sed "s/@LV2NAME@/$(LV2NAME)/g" \
	    lv2ttl/manifest.ttl.in > $(BUILDDIR)manifest.ttl
ifneq ($(BUILDOPENGL), no)
	sed "s/@LV2NAME@/$(LV2NAME)/g;s/@LIB_EXT@/$(LIB_EXT)/g;s/@URI_SUFFIX@//g" \
	    lv2ttl/manifest.lv2.ttl.in >> $(BUILDDIR)manifest.ttl
	sed "s/@LV2NAME@/$(LV2NAME)/g;s/@LIB_EXT@/$(LIB_EXT)/g;s/@UI_TYPE@/$(UI_TYPE)/;s/@LV2GUI@/$(LV2GUI)/g" \
	    lv2ttl/manifest.gl.ttl.in >> $(BUILDDIR)manifest.ttl
endif
ifneq ($(BUILDGTK), no)
	sed "s/@LV2NAME@/$(LV2NAME)/g;s/@LIB_EXT@/$(LIB_EXT)/g;s/@URI_SUFFIX@/_gtk/g" \
	    lv2ttl/manifest.lv2.ttl.in >> $(BUILDDIR)manifest.ttl
	sed "s/@LV2NAME@/$(LV2NAME)/g;s/@LIB_EXT@/$(LIB_EXT)/g;s/@LV2GTK@/$(LV2GTK)/g" \
	    lv2ttl/manifest.gtk.ttl.in >> $(BUILDDIR)manifest.ttl
endif

$(BUILDDIR)$(LV2NAME).ttl: lv2ttl/$(LV2NAME).ttl.in lv2ttl/$(LV2NAME).lv2.ttl.in lv2ttl/$(LV2NAME).gui.ttl.in Makefile
	@mkdir -p $(BUILDDIR)
	sed "s/@LV2NAME@/$(LV2NAME)/g" \
	    lv2ttl/$(LV2NAME).ttl.in > $(BUILDDIR)$(LV2NAME).ttl
ifneq ($(BUILDGTK), no)
	sed "s/@UI_URI_SUFFIX@/_gtk/;s/@UI_TYPE@/ui:GtkUI/;s/@UI_REQ@//;s/@URI_SUFFIX@/_gtk/g" \
	    lv2ttl/$(LV2NAME).gui.ttl.in >> $(BUILDDIR)$(LV2NAME).ttl
endif
ifneq ($(BUILDOPENGL), no)
	sed "s/@UI_URI_SUFFIX@/_gl/;s/@UI_TYPE@/$(UI_TYPE)/;s/@UI_REQ@/$(LV2UIREQ)/;s/@URI_SUFFIX@//g" \
	    lv2ttl/$(LV2NAME).gui.ttl.in >> $(BUILDDIR)$(LV2NAME).ttl
	sed "s/@URI_SUFFIX@//g;s/@NAME_SUFFIX@//g;s/@SISCOUI@/ui_gl/g" \
	  lv2ttl/$(LV2NAME).lv2.ttl.in >> $(BUILDDIR)$(LV2NAME).ttl
endif
ifneq ($(BUILDGTK), no)
	sed "s/@URI_SUFFIX@/_gtk/g;s/@NAME_SUFFIX@/ GTK/g;s/@SISCOUI@/ui_gtk/g" \
	  lv2ttl/$(LV2NAME).lv2.ttl.in >> $(BUILDDIR)$(LV2NAME).ttl
endif


$(BUILDDIR)$(LV2NAME)$(LIB_EXT): src/sisco.c src/uris.h
	@mkdir -p $(BUILDDIR)
	$(CC) $(CPPFLAGS) $(LV2CFLAGS) -std=c99 \
	  -o $(BUILDDIR)$(LV2NAME)$(LIB_EXT) src/sisco.c \
	  -shared $(LV2LDFLAGS) $(LDFLAGS)

sisco_UISRC= zita-resampler/resampler.cc zita-resampler/resampler-table.cc

$(BUILDDIR)$(LV2GTK)$(LIB_EXT): gui/sisco.c $(sisco_UISRC) \
    zita-resampler/resampler.h zita-resampler/resampler-table.h src/uris.h
$(BUILDDIR)$(LV2GUI)$(LIB_EXT): gui/sisco.c $(sisco_UISRC) \
    zita-resampler/resampler.h zita-resampler/resampler-table.h src/uris.h

$(BUILDDIR)x42-scope$(EXE_EXT): gui/sisco.c $(sisco_UISRC) \
    zita-resampler/resampler.h zita-resampler/resampler-table.h src/uris.h \
    src/sisco.c lv2ttl/jack_4chan.h

$(eval x42_scope_JACKSRC = $(sisco_UISRC) src/sisco.c)
x42_scope_JACKGUI = gui/sisco.c
x42_scope_LV2HTTL = lv2ttl/jack_4chan.h
x42_scope_JACKDESC = lv2ui_descriptor

-include $(RW)robtk.mk

###############################################################################
# install/uninstall/clean target definitions

install-bin: $(BUILDDIR)x42-scope$(EXE_EXT)
	install -d $(DESTDIR)$(bindir)
	install -m755 $(BUILDDIR)x42-scope$(EXE_EXT)  $(DESTDIR)$(bindir)/

install-man: x42-scope.1
	install -d $(DESTDIR)$(man1dir)
	install -m644 x42-scope.1 $(DESTDIR)$(man1dir)

uninstall-bin:
	rm -f $(DESTDIR)$(bindir)/x42-scope$(EXE_EXT)
	-rmdir $(DESTDIR)$(bindir)

uninstall-man:
	rm -f $(DESTDIR)$(man1dir)/x42-scope.1
	-rmdir $(DESTDIR)$(man1dir)
	-rmdir $(DESTDIR)$(mandir)

install-lv2: all
	install -d $(DESTDIR)$(LV2DIR)/$(BUNDLE)
	install -m755 $(targets) $(DESTDIR)$(LV2DIR)/$(BUNDLE)
	install -m644 $(BUILDDIR)manifest.ttl $(BUILDDIR)$(LV2NAME).ttl $(DESTDIR)$(LV2DIR)/$(BUNDLE)

uninstall-lv2:
	rm -f $(DESTDIR)$(LV2DIR)/$(BUNDLE)/manifest.ttl
	rm -f $(DESTDIR)$(LV2DIR)/$(BUNDLE)/$(LV2NAME).ttl
	rm -f $(DESTDIR)$(LV2DIR)/$(BUNDLE)/$(LV2NAME)$(LIB_EXT)
	rm -f $(DESTDIR)$(LV2DIR)/$(BUNDLE)/$(LV2GUI)$(LIB_EXT)
	rm -f $(DESTDIR)$(LV2DIR)/$(BUNDLE)/$(LV2GTK)$(LIB_EXT)
	-rmdir $(DESTDIR)$(LV2DIR)/$(BUNDLE)

install: install-lv2 install-man install-bin

uninstall: uninstall-lv2 uninstall-man uninstall-bin

clean:
	rm -f $(BUILDDIR)manifest.ttl $(BUILDDIR)$(LV2NAME).ttl \
	  $(BUILDDIR)$(LV2NAME)$(LIB_EXT) \
	  $(BUILDDIR)$(LV2GUI)$(LIB_EXT)  \
	  $(BUILDDIR)$(LV2GTK)$(LIB_EXT)
	rm -f $(BUILDDIR)/x42-scope$(EXE_EXT)
	rm -rf $(BUILDDIR)*.dSYM
	-test -d $(BUILDDIR) && rmdir $(BUILDDIR) || true

distclean: clean
	rm -f cscope.out cscope.files tags

.PHONY: clean all install uninstall distclean \
	install-bin install-man install-lv2 \
	uninstall-bin uninstall-man uninstall-lv2 \
	submodule_check submodules submodule_update submodule_pull
