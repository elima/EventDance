MAINTAINERCLEANFILES = \
	Makefile.in

AM_CFLAGS = \
	$(GLIB_CFLAGS) \
	-Wall -g3 -O0 -ggdb \
	-DPKGDATADIR="\"$(pkgdatadir)\"" \
	-I$(top_srcdir)/evd

TEST_PROGS =

noinst_PROGRAMS = \
	evd-tester \
	test-socket-context

# evd-tester
evd_tester_CFLAGS = \
	$(AM_CFLAGS)

evd_tester_LDADD = \
	$(GLIB_LIBS) \
	$(top_srcdir)/evd/libevd-@EVD_API_VERSION@.la

evd_tester_SOURCES = \
	evd-tester.c \
	$(TEST_PROGS)

# test-socket-context
test_socket_context_CFLAGS = \
	$(AM_CFLAGS)

test_socket_context_LDADD = \
	$(GLIB_LIBS) \
	$(top_srcdir)/evd/libevd-@EVD_API_VERSION@.la

test_socket_context_SOURCES = \
	test-socket-context.c

test: evd-tester
	./evd-tester
#	./test-socket-context
