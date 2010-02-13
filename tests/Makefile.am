MAINTAINERCLEANFILES = \
	Makefile.in

AM_CFLAGS = \
	$(GLIB_CFLAGS) \
	-Wall -g3 -O0 -ggdb \
	-DPKGDATADIR="\"$(pkgdatadir)\"" \
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
	evd-tester \
	test-socket \
	test-json-filter \
	test-json-socket \
	test-resolver \
	test-socket-context

# evd-tester
evd_tester_CFLAGS = $(AM_CFLAGS)
evd_tester_LDADD = $(AM_LIBS)
evd_tester_SOURCES = evd-tester.c

# test-socket-context
test_socket_context_CFLAGS = $(AM_CFLAGS)
test_socket_context_LDADD = $(AM_LIBS)
test_socket_context_SOURCES = test-socket-context.c

# test-socket
test_socket_CFLAGS = $(AM_CFLAGS)
test_socket_LDADD = $(AM_LIBS)
test_socket_SOURCES = test-socket.c

# test-resolver
test_resolver_CFLAGS = $(AM_CFLAGS)
test_resolver_LDADD = $(AM_LIBS)
test_resolver_SOURCES = test-resolver.c

# test-json-filter
test_json_filter_CFLAGS = $(AM_CFLAGS)
test_json_filter_LDADD = $(AM_LIBS)
test_json_filter_SOURCES = test-json-filter.c

# test-json-socket
test_json_socket_CFLAGS = $(AM_CFLAGS)
test_json_socket_LDADD = $(AM_LIBS)
test_json_socket_SOURCES = test-json-socket.c

test: evd-tester
	./evd-tester
