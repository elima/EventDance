MAINTAINERCLEANFILES = \
	Makefile.in

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = evd.pc

library_includedir=$(includedir)/@EVD_API_NAME@
library_include_HEADERS = evd.h

CLEANFILES = *~ evd.pc

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
	evd-stream.c \
	evd-socket-manager.c \
	evd-resolver-request.c \
	evd-resolver.c \
	evd-socket.c \
	evd-json-filter.c \
	evd-json-socket.c \
	evd-socket-group.c \
	evd-service.c \
	evd-reproxy-backend.c \
	evd-reproxy.c \
	evd-tls-common.c \
	evd-tls-session.c

source_h = \
	evd.h \
	evd-utils.h \
	evd-stream.h \
	evd-resolver-request.h \
	evd-resolver.h \
	evd-socket.h \
	evd-json-filter.h \
	evd-json-socket.h \
	evd-socket-group.h \
	evd-service.h \
	evd-reproxy.h \
	evd-tls-session.h

source_h_priv = \
	evd-marshal.h \
	evd-socket-manager.h \
	evd-socket-protected.h \
	evd-socket-group-protected.h \
	evd-reproxy-backend.h \
	evd-service-protected.h \
	evd-tls-common.h

lib@EVD_API_NAME@_la_LIBADD = \
	$(GLIB_LIBS) \
	$(TLS_LIBS)

lib@EVD_API_NAME@_la_CFLAGS  = \
	$(AM_CFLAGS) \
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

BUILT_GIRSOURCES =

@EVD_GIR_API_NAME@.gir: $(INTROSPECTION_SCANNER) lib@EVD_API_NAME@.la
	$(INTROSPECTION_SCANNER) -v \
		--namespace @EVD_GIR_NAMESPACE@ --nsversion=@EVD_VERSION@ \
		--c-include='@EVD_API_NAME@/evd.h' \
		--include=GObject-2.0 \
		--include=Gio-2.0 \
		--library=lib@EVD_API_NAME@.la \
	        --output $@ \
		$(evd_HEADERS)

BUILT_GIRSOURCES += @EVD_GIR_API_NAME@.gir

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
