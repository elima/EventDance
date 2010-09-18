SUBDIRS = \
	evd \
	tests \
	examples

if BUILD_GTK_DOC
SUBDIRS += doc
endif

DIST_SUBDIRS = \
	evd \
	tests \
	doc \
	examples

EXTRA_DIST = \
	autogen.sh \
	m4/introspection.m4

ACLOCAL_AMFLAGS = -I m4
DISTCHECK_CONFIGURE_FLAGS = --enable-introspection

CLEANFILES = *~

MAINTAINERCLEANFILES = \
	Makefile.in aclocal.m4 configure config.guess config.sub \
	depcomp install-sh ltmain.sh missing mkinstalldirs config.h.in \
	stamp-h.in compile gtk-doc.make

DISTCLEANFILES = \
	cscope.files cscope.out

cscope.files:
	find src -name '*.[ch]' > $@

cscope.out: cscope.files
	cscope -b

# test targets
test: tests/Makefile
	make -C tests/ test
