MAINTAINERCLEANFILES = \
	Makefile.in

examples_common_dir = `pwd`/common

AM_CFLAGS = \
	$(GLIB_CFLAGS) \
	$(TLS_CFLAGS) \
	$(UUID_CFLAGS) \
	$(JSON_CFLAGS) \
	$(SOUP_CFLAGS) \
	-DEXAMPLES_COMMON_DIR="\"$(examples_common_dir)\"" \
	-I$(top_srcdir)/evd

if ENABLE_DEBUG
AM_CFLAGS += -Werror -g3 -O0 -ggdb
else
AM_CFLAGS += -DG_DISABLE_ASSERT -DG_DISABLE_CHECKS
endif

AM_LIBS = \
	$(GLIB_LIBS) \
	$(TLS_LIBS) \
	$(UUID_LIBS) \
	$(JSON_LIBS) \
	$(SOUP_LIBS) \
	-lgcrypt \
	$(top_srcdir)/evd/libevd-@EVD_API_VERSION@.la

if HAVE_GIO_UNIX
AM_LIBS += \
	$(GIO_UNIX_LIBS)

AM_CFLAGS += \
	$(GIO_UNIX_CFLAGS) \
	-DHAVE_GIO_UNIX
endif

noinst_PROGRAMS = \
	ping-server \
	dbus-bridge

# ping-server
ping_server_CFLAGS = $(AM_CFLAGS) -DHAVE_JS
ping_server_LDADD = $(AM_LIBS)
ping_server_SOURCES = ping-server.c

# dbus-bridge
dbus_bridge_CFLAGS = $(AM_CFLAGS) -DHAVE_JS
dbus_bridge_LDADD = $(AM_LIBS)
dbus_bridge_SOURCES = dbus-bridge.c
