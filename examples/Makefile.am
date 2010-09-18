MAINTAINERCLEANFILES = \
	Makefile.in

AM_CFLAGS = \
	$(GLIB_CFLAGS) \
	$(SOUP_CFLAGS) \
	-Wall -g3 -O0 -ggdb \
	-I$(top_srcdir)/evd

AM_LIBS = \
	$(GLIB_LIBS) \
	$(top_srcdir)/evd/libevd-@EVD_API_VERSION@.la

if HAVE_GIO_UNIX
AM_LIBS += \
	$(GIO_UNIX_LIBS)

AM_CFLAGS += \
	$(GIO_UNIX_CFLAGS) \
	-DHAVE_GIO_UNIX
endif

noinst_PROGRAMS = \
	ping-server

# ping-server
ping_server_CFLAGS = $(AM_CFLAGS) -DHAVE_JS
ping_server_LDADD = $(AM_LIBS)
ping_server_SOURCES = ping-server.c
