AC_PREREQ(2.61)

# package version number (not shared library version)
# odd micro numbers indicate in-progress development
# even micro numbers indicate released versions
m4_define(evd_version_major, 0)
m4_define(evd_version_minor, 1)
m4_define(evd_version_micro, 0)

m4_define([evd_version],
          [evd_version_major.evd_version_minor.evd_version_micro])
m4_define([evd_api_version],
          [evd_version_major.evd_version_minor])

AC_INIT([EventDance],
	[evd_version],
	[http://eventdance.org])
AC_CONFIG_MACRO_DIR([build/autotools])
AC_CONFIG_HEADERS(config.h)

AM_INIT_AUTOMAKE([1.9])

EVD_API_VERSION=evd_api_version

AC_SUBST(EVD_API_VERSION)

# Check for programs
AC_PROG_LIBTOOL
AC_PROG_CC
AC_PROG_INSTALL
AM_PROG_CC_C_O

# enable pkg-config
PKG_PROG_PKG_CONFIG

# Required libraries
GIO_REQUIRED=2.23.1

PKG_CHECK_MODULES(GLIB, gio-2.0 >= $GIO_REQUIRED
		        glib-2.0 >= 2.22.0
		        gthread-2.0 >= 2.22.0
		        gobject-2.0 >= 2.22.0)

PKG_CHECK_MODULES([GIO_UNIX], [gio-unix-2.0 >= $GIO_REQUIRED],
        [HAVE_GIO_UNIX=yes],
        [HAVE_GIO_UNIX=no])
AM_CONDITIONAL(HAVE_GIO_UNIX, test x"$HAVE_GIO_UNIX" = x"yes")

# GObject-Introspection check
GOBJECT_INTROSPECTION_CHECK([0.6.4])

# Javascript support check
JS_CHECK([0.3])

# GTK Doc
GTK_DOC_CHECK([1.11])
AM_CONDITIONAL([BUILD_GTK_DOC], [test "x$enable_gtk_doc" = "xyes" || test ! -f "autogen.sh"])

# Output files
AC_OUTPUT([
	Makefile
	build/Makefile
	build/autotools/Makefile
	evd/Makefile
	evd/evd.pc
	tests/Makefile
        doc/Makefile
        doc/reference/Makefile
])

echo ""
echo "   EventDance $VERSION"
echo "   ====================="
echo ""
echo "    Build introspection data:   ${enable_introspection}"
echo "     Build API documentation:   ${enable_gtk_doc}"
echo "  Build Javascript libraries:   ${enable_js}"
echo ""

