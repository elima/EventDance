MAINTAINERCLEANFILES = \
	Makefile.in

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = @EVD_API_NAME@.pc

library_includedir=$(includedir)/@EVD_API_NAME@
library_include_HEADERS = evd.h

CLEANFILES = *~ @EVD_API_NAME@.pc

AM_CFLAGS = $(GLIB_CFLAGS) -Wall -Werror -g3 -O0 -ggdb \
	-DPKGDATADIR="\"$(pkgdatadir)\""

evd-marshal.h: evd-marshal.list
	glib-genmarshal --header \
		--prefix=evd_marshal evd-marshal.list > evd-marshal.h

evd-marshal.c: evd-marshal.list
	glib-genmarshal --body \
		--prefix=evd_marshal evd-marshal.list > evd-marshal.c

BUILT_SOURCES = evd-marshal.c evd-marshal.h

# libraries
lib_LTLIBRARIES = lib@EVD_API_NAME@.la

# libevd
source_c = \
	evd-utils.c \
	evd-marshal.c \
	evd-resolver-request.c \
	evd-resolver.c \
	evd-socket.c \
	evd-socket-manager.c \
	evd-socket-input-stream.c \
	evd-socket-output-stream.c \
	evd-tls-input-stream.c \
	evd-tls-output-stream.c \
	evd-stream-throttle.c \
	evd-buffered-input-stream.c \
	evd-buffered-output-stream.c \
	evd-throttled-input-stream.c \
	evd-throttled-output-stream.c \
	evd-json-filter.c \
	evd-service.c \
	evd-tls-common.c \
	evd-tls-dh-generator.c \
	evd-tls-session.c \
	evd-tls-certificate.c \
	evd-tls-credentials.c \
	evd-connection.c \
	evd-io-stream-group.c \
	evd-http-connection.c \
	evd-transport.c \
	evd-peer.c \
	evd-long-polling.c \
	evd-connection-pool.c

source_h = \
	evd.h \
	evd-error.h \
	evd-utils.h \
	evd-resolver-request.h \
	evd-resolver.h \
	evd-socket.h \
	evd-socket-input-stream.h \
	evd-socket-output-stream.h \
	evd-tls-input-stream.h \
	evd-tls-output-stream.h \
	evd-stream-throttle.h \
	evd-buffered-input-stream.h \
	evd-buffered-output-stream.h \
	evd-throttled-input-stream.h \
	evd-throttled-output-stream.h \
	evd-json-filter.h \
	evd-service.h \
	evd-tls-session.h \
	evd-tls-certificate.h \
	evd-tls-credentials.h \
	evd-connection.h \
	evd-io-stream-group.h \
	evd-http-connection.h \
	evd-transport.h \
	evd-peer.h \
	evd-long-polling.h \
	evd-connection-pool.h

source_h_priv = \
	evd-marshal.h \
	evd-socket-manager.h \
	evd-socket-protected.h \
	evd-tls-common.h \
	evd-tls-dh-generator.h

lib@EVD_API_NAME@_la_LIBADD = \
	$(GLIB_LIBS) \
	$(UUID_LIBS) \
	$(SOUP_LIBS) \
	$(TLS_LIBS)

lib@EVD_API_NAME@_la_CFLAGS  = \
	$(AM_CFLAGS) \
	$(UUID_CFLAGS) \
	$(SOUP_CFLAGS) \
	$(TLS_CFLAGS)

if HAVE_GIO_UNIX
lib@EVD_API_NAME@_la_LIBADD += \
	$(GIO_UNIX_LIBS)

lib@EVD_API_NAME@_la_CFLAGS += \
	$(GIO_UNIX_CFLAGS) \
	-DHAVE_GIO_UNIX \
	-DHAVE_JS
endif

lib@EVD_API_NAME@_la_LDFLAGS = \
	-version-info 0:1:0 \
	-no-undefined

lib@EVD_API_NAME@_la_SOURCES = \
	$(source_c) \
	$(source_h) \
	$(source_h_priv)

evddir = $(includedir)/@EVD_API_NAME@
evd_HEADERS = \
	$(source_h) \
	$(source_h_priv)


# introspection support
if HAVE_INTROSPECTION

-include $(INTROSPECTION_MAKEFILE)
INTROSPECTION_GIRS =
INTROSPECTION_SCANNER_ARGS = --add-include-path=$(srcdir)
INTROSPECTION_COMPILER_ARGS = --includedir=$(srcdir)

introspection_sources = $(evd_HEADERS) $(source_c)

@EVD_GIR_API_NAME@.gir: lib@EVD_API_NAME@.la Makefile
@EVD_GIR_TARGET_NAME@_INCLUDES = GObject-2.0 Gio-2.0 Soup-2.4
@EVD_GIR_TARGET_NAME@_CFLAGS = $(INCLUDES)
@EVD_GIR_TARGET_NAME@_LIBS = lib@EVD_API_NAME@.la uuid
@EVD_GIR_TARGET_NAME@_FILES = $(addprefix $(srcdir)/,$(introspection_sources))

INTROSPECTION_GIRS += @EVD_GIR_API_NAME@.gir

girdir = $(datadir)/gir-1.0
dist_gir_DATA = $(INTROSPECTION_GIRS)

typelibdir = $(libdir)/girepository-1.0
typelib_DATA = $(INTROSPECTION_GIRS:.gir=.typelib)

CLEANFILES += $(dist_gir_DATA) $(typelib_DATA)
endif

maintainer-clean-local:
	rm -rf tmp-introspect*
