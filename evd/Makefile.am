MAINTAINERCLEANFILES = \
	Makefile.in

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = @EVD_API_NAME@.pc

library_includedir=$(includedir)/@EVD_API_NAME@
library_include_HEADERS = evd.h

jslibdir = $(datadir)/@EVD_API_NAME@/js

CLEANFILES = *~ @EVD_API_NAME@.pc

AM_CFLAGS = $(GLIB_CFLAGS) -Wall \
	-DPKGDATADIR="\"$(pkgdatadir)\"" \
	-DJSLIBDIR="\"$(jslibdir)\""

if ENABLE_TESTS
AM_CFLAGS += -DENABLE_TESTS
endif

if ENABLE_DEBUG
AM_CFLAGS += -Werror -g3 -O0 -ggdb -D_GNU_SOURCE
else
AM_CFLAGS += -DG_DISABLE_ASSERT -DG_DISABLE_CHECKS
endif

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
	evd-error.c \
	evd-utils.c \
	evd-resolver.c \
	evd-poll.c \
	evd-socket.c \
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
	evd-tls-privkey.c \
	evd-tls-credentials.c \
	evd-io-stream.c \
	evd-connection.c \
	evd-io-stream-group.c \
	evd-http-connection.c \
	evd-web-service.c \
	evd-transport.c \
	evd-peer.c \
	evd-peer-manager.c \
	evd-longpolling-server.c \
	evd-websocket-protocol.c \
	evd-websocket-common.c \
	evd-websocket00.c \
	evd-websocket08.c \
	evd-websocket-server.c \
	evd-connection-pool.c \
	evd-reproxy.c \
	evd-web-selector.c \
	evd-web-transport-server.c \
	evd-http-message.c \
	evd-http-request.c \
	evd-web-dir.c \
	evd-dbus-agent.c \
	evd-dbus-bridge.c \
	evd-dbus-daemon.c \
	evd-jsonrpc.c \
	evd-tls-cipher.c \
	evd-pki.c \
	evd-pki-privkey.c \
	evd-pki-pubkey.c \
	evd-daemon.c \
	evd-http-chunked-decoder.c \
	evd-jsonrpc-http-client.c \
	evd-jsonrpc-http-server.c

source_h = \
	evd.h \
	evd-error.h \
	evd-utils.h \
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
	evd-tls-privkey.h \
	evd-tls-credentials.h \
	evd-io-stream.h \
	evd-connection.h \
	evd-io-stream-group.h \
	evd-http-connection.h \
	evd-web-service.h \
	evd-transport.h \
	evd-peer.h \
	evd-peer-manager.h \
	evd-longpolling-server.h \
	evd-websocket-server.h \
	evd-connection-pool.h \
	evd-reproxy.h \
	evd-web-selector.h \
	evd-web-transport-server.h \
	evd-http-message.h \
	evd-http-request.h \
	evd-web-dir.h \
	evd-dbus-agent.h \
	evd-dbus-bridge.h \
	evd-dbus-daemon.h \
	evd-jsonrpc.h \
	evd-tls-cipher.h \
	evd-pki-common.h \
	evd-pki.h \
	evd-pki-privkey.h \
	evd-pki-pubkey.h \
	evd-daemon.h \
	evd-http-chunked-decoder.h \
	evd-jsonrpc-http-client.h \
	evd-jsonrpc-http-server.h

source_h_priv = \
	evd-poll.h \
	evd-tls-common.h \
	evd-tls-dh-generator.h \
	evd-websocket-common.h \
	evd-websocket00.h \
	evd-websocket08.h

lib@EVD_API_NAME@_la_LIBADD = \
	$(GLIB_LIBS) \
	$(UUID_LIBS) \
	$(SOUP_LIBS) \
	$(TLS_LIBS) \
	$(JSON_LIBS) \
	-lgcrypt

lib@EVD_API_NAME@_la_CFLAGS  = \
	$(AM_CFLAGS) \
	$(UUID_CFLAGS) \
	$(SOUP_CFLAGS) \
	$(TLS_CFLAGS) \
	$(JSON_CFLAGS)

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

nodist_lib@EVD_API_NAME@_la_SOURCES = \
	evd-marshal.c \
	evd-marshal.h

evddir = $(includedir)/@EVD_API_NAME@
evd_HEADERS = \
	$(source_h) \
	$(source_h_priv)

jslib_DATA = \
	js/evdWebTransport.js \
	js/evdDBusBridge.js \
	js/evdJsonrpc.js

# introspection support
if HAVE_INTROSPECTION

-include $(INTROSPECTION_MAKEFILE)
INTROSPECTION_GIRS =
INTROSPECTION_SCANNER_ARGS = --add-include-path=$(srcdir) --warn-all
INTROSPECTION_COMPILER_ARGS = --includedir=$(srcdir)

introspection_sources = $(evd_HEADERS) $(source_c)

@EVD_GIR_API_NAME@.gir: lib@EVD_API_NAME@.la Makefile
@EVD_GIR_TARGET_NAME@_INCLUDES = GObject-2.0 Gio-2.0 Soup-2.4 Json-1.0
@EVD_GIR_TARGET_NAME@_CFLAGS = $(INCLUDES) $(JSON_CFLAGS)
@EVD_GIR_TARGET_NAME@_LIBS = @EVD_API_NAME@ uuid gcrypt
@EVD_GIR_TARGET_NAME@_FILES = $(addprefix $(srcdir)/,$(introspection_sources))
@EVD_GIR_TARGET_NAME@_SCANNERFLAGS = \
	--warn-all \
	--c-include='evd.h' \
	--pkg-export=@EVD_API_NAME@

INTROSPECTION_GIRS += @EVD_GIR_API_NAME@.gir

girdir = @INTROSPECTION_GIRDIR@
gir_DATA = $(INTROSPECTION_GIRS)

typelibdir = @INTROSPECTION_TYPELIBDIR@
typelib_DATA = $(INTROSPECTION_GIRS:.gir=.typelib)

CLEANFILES += $(dist_gir_DATA) $(typelib_DATA)
endif

maintainer-clean-local:
	rm -rf tmp-introspect*

EXTRA_DIST = \
	evd-marshal.list \
	js/evdWebTransport.js \
	js/evdDBusBridge.js \
	js/evdJsonrpc.js
