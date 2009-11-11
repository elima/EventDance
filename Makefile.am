SUBDIRS = \
	build \
	evd \
	tests

if BUILD_GTK_DOC
SUBDIRS += doc
endif

DIST_SUBDIRS = \
	build \
	evd \
	tests

if BUILD_GTK_DOC
DIST_SUBDIRS += doc
endif

EXTRA_DIST = \
	autogen.sh

ACLOCAL_AMFLAGS = -I build/autotools

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
