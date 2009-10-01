MAINTAINERCLEANFILES = \
	Makefile.in

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = evd.pc

library_includedir=$(includedir)/evd
library_include_HEADERS = evd.h

#see gcc man page for description: http://www.google.com/search?q=man+gcc
AM_CFLAGS = $(GLIB_CFLAGS) -Wall -Werror -g3 -O0 -ggdb -DPKGDATADIR="\"$(pkgdatadir)\""

lib_LTLIBRARIES = libevd.la

libevd_la_LIBADD = \
	$(GLIB_LIBS)

libevd_la_CFLAGS  = \
	$(AM_CFLAGS)

libevd_la_LDFLAGS = \
	-version-info 0:1:0 \
	-no-undefined

libevd_la_SOURCES = \
	marshal.h \
	marshal.c \
	evd-socket-manager.h \
	evd-socket-manager.c \
	evd-socket.h \
	evd-socket.c \
	evd.h

CLEANFILES = *~ evd.pc

BUILT_SOURCES = marshal.c marshal.h

marshal.h: marshal.list
	glib-genmarshal --header \
		--prefix=evd_marshal marshal.list > marshal.h

marshal.c: marshal.list
	glib-genmarshal --body \
		--prefix=evd_marshal marshal.list > marshal.c
