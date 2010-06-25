MAINTAINERCLEANFILES = \
	Makefile.in

AM_CFLAGS = \
	$(GLIB_CFLAGS) \
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

noinst_PROGRAMS =
#	test-all \
#	test-socket \
#	test-json-filter \
#	test-resolver \
#	test-socket-context
#	test-http-connection

# test-all
test_all_CFLAGS = $(AM_CFLAGS) -DHAVE_JS
test_all_LDADD = $(AM_LIBS)
test_all_SOURCES = test-all.c

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


if HAVE_JS
noinst_PROGRAMS += test-all-js

# test-all-js
test_all_js_CFLAGS = $(AM_CFLAGS) $(GJS_CFLAGS)
test_all_js_LDADD = $(AM_LIBS) $(GJS_LIBS)
test_all_js_SOURCES = test-all-js.c
endif

test: test-all
	./test-all
