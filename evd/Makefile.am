MAINTAINERCLEANFILES = \
	Makefile.in

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = evd.pc

library_includedir=$(includedir)/evd-@EVD_API_VERSION@
library_include_HEADERS = evd.h

CLEANFILES = *~ evd.pc

#see gcc man page for description: http://www.google.com/search?q=man+gcc
AM_CFLAGS = $(GLIB_CFLAGS) -Wall -Werror -g3 -O0 -ggdb -DPKGDATADIR="\"$(pkgdatadir)\""

evd-marshal.h: evd-marshal.list
	glib-genmarshal --header \
		--prefix=evd_marshal evd-marshal.list > evd-marshal.h

evd-marshal.c: evd-marshal.list
	glib-genmarshal --body \
		--prefix=evd_marshal evd-marshal.list > evd-marshal.c

BUILT_SOURCES = evd-marshal.c evd-marshal.h

INCLUDES = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/evd-@EVD_API_VERSION@

# libraries
lib_LTLIBRARIES = libevd-@EVD_API_VERSION@.la

# libevd
source_c = \
	evd-utils.c \
	evd-marshal.c \
	evd-stream.c \
	evd-socket-manager.c \
	evd-resolver.c \
	evd-socket.c \
	evd-json-filter.c \
	evd-json-socket.c \
	evd-inet-socket.c \
	evd-socket-group.c \
	evd-service.c \
	evd-reproxy-backend.c \
	evd-reproxy.c

source_h = \
	evd-utils.h \
	evd-stream.h \
	evd-resolver.h \
	evd-socket.h \
	evd-json-filter.h \
	evd-json-socket.h \
	evd-inet-socket.h \
	evd-socket-group.h \
	evd-service.h \
	evd-reproxy.h \
	evd.h

source_h_priv = \
	evd-marshal.h \
	evd-socket-manager.h \
	evd-socket-protected.h \
	evd-socket-group-protected.h \
	evd-reproxy-backend.h \
	evd-service-protected.h

libevd_@EVD_API_VERSION@_la_LIBADD = \
	$(GLIB_LIBS)

libevd_@EVD_API_VERSION@_la_CFLAGS  = \
	$(AM_CFLAGS)

if HAVE_GIO_UNIX
libevd_@EVD_API_VERSION@_la_LIBADD += \
	$(GIO_UNIX_LIBS)

libevd_@EVD_API_VERSION@_la_CFLAGS += \
	$(GIO_UNIX_CFLAGS) \
	-DHAVE_GIO_UNIX
endif

libevd_@EVD_API_VERSION@_la_LDFLAGS = \
	-version-info 0:1:0 \
	-no-undefined

libevd_@EVD_API_VERSION@_la_SOURCES = \
	$(source_c) \
	$(source_h) \
	$(source_h_priv)

evddir = $(includedir)/evd-@EVD_API_VERSION@
evd_HEADERS = \
	$(source_h) \
	$(source_h_priv)

# introspection support
if HAVE_INTROSPECTION
BUILT_GIRSOURCES =

Evd-@EVD_API_VERSION@.gir: $(INTROSPECTION_SCANNER) libevd-@EVD_API_VERSION@.la
	$(INTROSPECTION_SCANNER) -v \
		--namespace Evd --nsversion=0.1.0 \
		$(INCLUDES) \
		--c-include='evd-@EVD_API_VERSION@/evd.h' \
		--include=GObject-2.0 \
		--include=Gio-2.0 \
		--library=libevd-@EVD_API_VERSION@.la \
		--libtool="$(top_builddir)/libtool" \
	        --pkg gobject-2.0 \
	        --output $@ \
		$(evd_HEADERS)

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

maintainer-clean-local:
	rm -rf tmp-introspect*
