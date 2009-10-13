MAINTAINERCLEANFILES = \
	Makefile.in

AM_CFLAGS = $(GLIB_CFLAGS) -Wall -g3 -O0 -ggdb -DPKGDATADIR="\"$(pkgdatadir)\""

bin_PROGRAMS = \
	test-socket \
	test-socket-context

# test-socket
test_socket_CFLAGS = \
	-I. \
	-I$(top_srcdir) \
	-I$(top_srcdir)/evd \
	$(AM_CFLAGS)

test_socket_LDADD = \
	$(GLIB_LIBS) \
	$(top_srcdir)/evd/libevd-@EVD_API_VERSION@.la

test_socket_SOURCES = \
	test-socket.c

# test-socket-context
test_socket_context_CFLAGS = \
	-I. \
	-I$(top_srcdir) \
	-I$(top_srcdir)/evd \
	$(AM_CFLAGS)

test_socket_context_LDADD = \
	$(GLIB_LIBS) \
	$(top_srcdir)/evd/libevd-@EVD_API_VERSION@.la

test_socket_context_SOURCES = \
	test-socket-context.c
