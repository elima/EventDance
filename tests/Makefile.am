MAINTAINERCLEANFILES = \
	Makefile.in

tests_dir = `pwd`/

AM_CFLAGS = \
	$(GLIB_CFLAGS) \
	$(TLS_CFLAGS) \
	$(UUID_CFLAGS) \
	$(JSON_CFLAGS) \
	$(SOUP_CFLAGS) \
	-DTESTS_DIR="\"$(tests_dir)\"" \
	-I$(top_srcdir)/evd

if ENABLE_DEBUG
AM_CFLAGS += -g3 -O0 -ggdb
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

if ENABLE_TESTS

noinst_PROGRAMS = \
	test-all \
	test-json-filter \
	test-resolver \
	test-dbus-bridge \
	test-tls-cipher \
	test-pki

TESTS = \
	test-json-filter \
	test-resolver \
	test-dbus-bridge \
	test-tls-cipher \
	test-pki

# test-all
test_all_CFLAGS = $(AM_CFLAGS) -DHAVE_JS
test_all_LDADD = $(AM_LIBS)
test_all_SOURCES = test-all.c

# test-resolver
test_resolver_CFLAGS = $(AM_CFLAGS)
test_resolver_LDADD = $(AM_LIBS)
test_resolver_SOURCES = test-resolver.c

# test-json-filter
test_json_filter_CFLAGS = $(AM_CFLAGS)
test_json_filter_LDADD = $(AM_LIBS)
test_json_filter_SOURCES = test-json-filter.c

# test-json-filter
test_dbus_bridge_CFLAGS = $(AM_CFLAGS)
test_dbus_bridge_LDADD = $(AM_LIBS)
test_dbus_bridge_SOURCES = test-dbus-bridge.c

# test-tls-cipher
test_tls_cipher_CFLAGS = $(AM_CFLAGS)
test_tls_cipher_LDADD = $(AM_LIBS)
test_tls_cipher_SOURCES = test-tls-cipher.c

# test-pki
test_pki_CFLAGS = $(AM_CFLAGS)
test_pki_LDADD = $(AM_LIBS)
test_pki_SOURCES = test-pki.c

if HAVE_JS
noinst_PROGRAMS += test-all-js

# test-all-js
test_all_js_CFLAGS = $(AM_CFLAGS) $(GJS_CFLAGS)
test_all_js_LDADD = $(AM_LIBS) $(GJS_LIBS)
test_all_js_SOURCES = test-all-js.c
endif

endif # ENABLE_TESTS

EXTRA_DIST = \
	certs/openpgp-server.asc \
	certs/openpgp-server-key.asc \
	certs/x509-ca-key.pem \
	certs/x509-ca.pem \
	certs/x509-jane-key.pem \
	certs/x509-jane.p12 \
	certs/x509-jane.pem \
	certs/x509-mary-key.pem \
	certs/x509-mary.p12 \
	certs/x509-mary.pem \
	certs/x509-server-key.pem \
	certs/x509-server.pem \
	js/common/assert.js \
	js/common/test.js \
	js/testResolver.js \
	js/testTlsCertificate.js \
	dbus-daemon.conf
