AC_PREREQ(2.62)

m4_define([prj_name],       [EventDance])
m4_define([prj_short_name], [evd])
m4_define([prj_home],       [http://eventdance.org])

# package version number (not shared library version)
# odd micro numbers indicate in-progress development
# even micro numbers indicate released versions
m4_define([prj_version_major], [0])
m4_define([prj_version_minor], [1])
m4_define([prj_version_micro], [0])

m4_define([prj_version],
          [prj_version_major.prj_version_minor.prj_version_micro])
m4_define([prj_api_version],
          [prj_version_major.prj_version_minor])

AC_INIT([prj_name],
	[prj_version],
	[prj_home])

#AC_CONFIG_MACRO_DIR([build/autotools])
AC_CONFIG_HEADERS(config.h)
AC_CONFIG_MACRO_DIR([m4])

#AM_INIT_AUTOMAKE([-Wno-portability])
AM_INIT_AUTOMAKE

# EventDance specific definitions
m4_define([prj_gir_namespace], [Evd])

EVD_VERSION="prj_version"
AC_SUBST(EVD_VERSION)

EVD_API_VERSION="prj_api_version"
AC_SUBST(EVD_API_VERSION)

EVD_API_NAME="prj_short_name-prj_api_version"
AC_SUBST(EVD_API_NAME)

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

PKG_CHECK_MODULES(TLS, gnutls >= 2.6.5)

# GObject-Introspection check
GOBJECT_INTROSPECTION_CHECK([0.6.7])
if test "x$found_introspection" = "xyes"; then
   EVD_GIR_NAMESPACE="prj_gir_namespace"
   AC_SUBST(EVD_GIR_NAMESPACE)

   EVD_GIR_API_NAME="prj_gir_namespace"-"prj_api_version"
   AC_SUBST(EVD_GIR_API_NAME)

   EVD_GIR_TARGET_NAME="prj_gir_namespace"_"prj_version_major"_"prj_version_minor"_gir
   AC_SUBST(EVD_GIR_TARGET_NAME)
fi

# Javascript support check
JS_CHECK([0.3])

# GTK Doc
GTK_DOC_CHECK([1.11])
AM_CONDITIONAL([BUILD_GTK_DOC], [test "x$enable_gtk_doc" = "xyes" || test ! -f "autogen.sh"])

# Output files
AC_OUTPUT([
	Makefile
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

