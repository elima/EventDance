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

# Check for programs
AC_PROG_LIBTOOL
AC_PROG_CC
AC_PROG_INSTALL
AM_PROG_CC_C_O

# enable pkg-config
PKG_PROG_PKG_CONFIG

# Required libraries
PKG_CHECK_MODULES(GLIB, gio-2.0 >= 2.22.0
		        glib-2.0 >= 2.22.0
		        gthread-2.0 >= 2.22.0
		        gobject-2.0 >= 2.22.0)

# GObject-Introspection check
GOBJECT_INTROSPECTION_CHECK([0.6.4])

# Output files
AC_OUTPUT([
	Makefile
	build/Makefile
	build/autotools/Makefile
	evd/Makefile
	evd/evd.pc
	tests/Makefile
])

echo ""
echo "   EventDance $VERSION"
echo "   ====================="
echo ""
echo "    Build introspection data:   ${enable_introspection}"
echo ""

