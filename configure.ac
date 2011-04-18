#
# configure.ac
#
# EventDance, Peer-to-peer IPC library <http://eventdance.org>
#
# Copyright (C) 2009/2010/2011, Igalia S.L.
#
# Authors:
#   Eduardo Lima Mitev <elima@igalia.com>
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public License
# version 3, or (at your option) any later version as published by
# the Free Software Foundation.
#
# This library is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
# Lesser General Public License at http://www.gnu.org/licenses/lgpl-3.0.txt
# for more details.
#

AC_PREREQ(2.59)

m4_define([prj_name],       [EventDance])
m4_define([prj_short_name], [evd])
m4_define([prj_home],       [http://eventdance.org])

# package version number (not shared library version)
# odd micro numbers indicate in-progress development
# even micro numbers indicate released versions
m4_define([prj_version_major], [0])
m4_define([prj_version_minor], [1])
m4_define([prj_version_micro], [5])

m4_define([prj_version],
          [prj_version_major.prj_version_minor.prj_version_micro])
m4_define([prj_api_version],
          [prj_version_major.prj_version_minor])

AC_INIT([prj_name],
	[prj_version],
	[prj_home])

AC_CONFIG_HEADERS(config.h)
AC_CONFIG_MACRO_DIR([m4])

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
		        glib-2.0 >= 2.23.0
		        gthread-2.0 >= 2.23.0
		        gobject-2.0 >= 2.23.0)

PKG_CHECK_MODULES([GIO_UNIX], [gio-unix-2.0 >= $GIO_REQUIRED],
        [HAVE_GIO_UNIX=yes],
        [HAVE_GIO_UNIX=no])
AM_CONDITIONAL(HAVE_GIO_UNIX, test x"$HAVE_GIO_UNIX" = x"yes")

PKG_CHECK_MODULES(TLS, gnutls >= 2.6.5)
PKG_CHECK_MODULES(SOUP, libsoup-2.4 >= 2.22.0)
PKG_CHECK_MODULES(UUID, uuid >= 2.16.0)
PKG_CHECK_MODULES(JSON, json-glib-1.0 >= 0.10.0)

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

# Silent build
m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])])

# Tests
AC_ARG_ENABLE(tests,
        AS_HELP_STRING([--enable-tests[=@<:@no/yes@:>@]],
                [Enable automated unit and functional tests [default=yes]]),,
                [enable_tests=yes])

AM_CONDITIONAL(ENABLE_TESTS, test x"${enable_tests}" = x"yes")

# Output files
AC_OUTPUT([
	Makefile
	evd/Makefile
	evd/evd-0.1.pc
	tests/Makefile
        doc/Makefile
        doc/reference/Makefile
        examples/Makefile
])

echo ""
echo "   EventDance $VERSION"
echo "   ====================="
echo ""
echo "              Install prefix:   ${prefix}"
echo "    Build introspection data:   ${enable_introspection}"
echo "     Build API documentation:   ${enable_gtk_doc}"
echo "      Enable automated tests:   ${enable_tests}"
echo "     Enable Javascript tests:   ${enable_js}"
echo ""
