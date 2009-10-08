MAINTAINERCLEANFILES = \
	Makefile.in

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = evd.pc

library_includedir=$(includedir)/evd
library_include_HEADERS = evd.h

CLEANFILES = *~ evd.pc

#see gcc man page for description: http://www.google.com/search?q=man+gcc
AM_CFLAGS = $(GLIB_CFLAGS) -Wall -Werror -g3 -O0 -ggdb -DPKGDATADIR="\"$(pkgdatadir)\""

marshal.h: marshal.list
	glib-genmarshal --header \
		--prefix=evd_marshal marshal.list > marshal.h

marshal.c: marshal.list
	glib-genmarshal --body \
		--prefix=evd_marshal marshal.list > marshal.c

BUILT_SOURCES = marshal.c marshal.h

INCLUDES = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/evd

# libraries
lib_LTLIBRARIES = libevd-@EVD_API_VERSION@.la

# libevd
source_c = \
	marshal.c \
	evd-socket-manager.c \
	evd-socket.c \
	evd-inet-socket.c

source_h = \
	marshal.h \
	evd-socket.h \
	evd-inet-socket.h \
	evd.h

source_h_priv = \
	evd-socket-manager.h \
	evd-socket-protected.h

libevd_@EVD_API_VERSION@_la_LIBADD = \
	$(GLIB_LIBS)

libevd_@EVD_API_VERSION@_la_CFLAGS  = \
	$(AM_CFLAGS)

libevd_@EVD_API_VERSION@_la_LDFLAGS = \
	-version-info 0:1:0 \
	-no-undefined

libevd_@EVD_API_VERSION@_la_SOURCES = \
	$(source_c) \
	$(source_h) \
	$(source_h_priv)

evddir = $(includedir)/evd
evd_HEADERS = \
	$(source_h)

# introspection support
if HAVE_INTROSPECTION
BUILT_GIRSOURCES =

Evd-@EVD_API_VERSION@.gir: $(INTROSPECTION_SCANNER) libevd-@EVD_API_VERSION@.la
	$(INTROSPECTION_SCANNER) -v \
		--namespace Evd --nsversion=0.1.0 \
		$(INCLUDES) \
		--c-include='evd/evd.h' \
		--include=GObject-2.0 \
		--include=Gio-2.0 \
		--library=libevd-@EVD_API_VERSION@.la \
		--libtool="$(top_builddir)/libtool" \
	        --pkg gobject-2.0 \
	        --output $@ \
		$(evd_HEADERS) \
		$(source_c)

BUILT_GIRSOURCES += Evd-@EVD_API_VERSION@.gir

# INTROSPECTION_GIRDIR/INTROSPECTION_TYPELIBDIR aren't the right place to
# install anything - we need to install inside our prefix.
girdir = $(datadir)/gir-1.0
gir_DATA = $(BUILT_GIRSOURCES)

typelibsdir = $(libdir)/girepository-1.0/

typelibs_DATA = $(BUILT_GIRSOURCES:.gir=.typelib)

%.typelib: %.gir $(INTROSPECTION_COMPILER)
	$(QUIET_GEN) \
	LD_LIBRARY_PATH=.libs$${LD_LIBRARY_PATH:+:$$LD_LIBRARY_PATH} \
	$(INTROSPECTION_COMPILER) \
		--includedir=$(srcdir) \
		--includedir=. \
		$(INTROSPECTION_COMPILER_OPTS) $< -o $(@F)

CLEANFILES += $(BUILT_GIRSOURCES) $(typelibs_DATA)
endif
