dnl -*- mode: autoconf -*-
dnl Copyright 2010 Eduardo Lima Mitev <elima@igalia.com>
dnl
dnl This file is free software; the author(s) gives unlimited
dnl permission to copy and/or distribute it, with or without
dnl modifications, as long as this notice is preserved.
dnl

m4_define([_JS_CHECK_INTERNAL],
[
    if test x"$found_introspection" = x"yes"; then

    AC_BEFORE([AC_PROG_LIBTOOL],[$0])dnl setup libtool first
    AC_BEFORE([AM_PROG_LIBTOOL],[$0])dnl setup libtool first
    AC_BEFORE([LT_INIT],[$0])dnl setup libtool first

    dnl enable/disable js
    m4_if([$2], [require],
    [dnl
        enable_js=yes
    ],[dnl
        AC_ARG_ENABLE(js,
                  AS_HELP_STRING([--enable-js[=@<:@no/auto/yes@:>@]],
                                 [Enable Javascript library support using GJS engine and Gobject-Introspection]),,
                                 [enable_js=auto])
    ])dnl

    AC_MSG_CHECKING([for Javascript])

    dnl presence/version checking
    AS_CASE([$enable_js],
    [no], [dnl
        found_js="no (disabled, use --enable-js to enable)"
    ],dnl
    [yes],[dnl
        PKG_CHECK_EXISTS([gjs-1.0],,
                         AC_MSG_ERROR([gjs-1.0 is not installed]))
        PKG_CHECK_EXISTS([gjs-1.0 >= $1],
                         found_js=yes,
                         AC_MSG_ERROR([You need to have gjs >= $1 installed to build AC_PACKAGE_NAME]))
        PKG_CHECK_EXISTS([gjs-gi-1.0],,
                         AC_MSG_ERROR([gjs-gi-1.0 is not installed]))
        PKG_CHECK_EXISTS([gjs-gi-1.0 >= $1],
                         found_js=yes,
                         AC_MSG_ERROR([You need to have gjs-gi >= $1 installed to build AC_PACKAGE_NAME]))
    ],dnl
    [auto],[dnl
        PKG_CHECK_EXISTS([gjs-1.0 >= $1], found_js=yes, found_js=no)
    ],dnl
    [dnl
        AC_MSG_ERROR([invalid argument passed to --enable-js, should be one of @<:@no/auto/yes@:>@])
    ])dnl

    AC_MSG_RESULT([$found_js])

    else
        enable_js="no (requires introspection to be enabled)"
        found_js=no
    fi

    if test "x$found_js" = "xyes"; then
       PKG_CHECK_MODULES([GJS], gjs-1.0 >= 0.3)
    fi

    AM_CONDITIONAL(HAVE_JS, test "x$found_js" = "xyes")
])


dnl Usage:
dnl   JS_CHECK([minimum-gjs-version])
AC_DEFUN([JS_CHECK],
[
  _JS_CHECK_INTERNAL([$1])
])

dnl Usage:
dnl   JS_REQUIRE([minimum-gjs-version])
AC_DEFUN([JS_REQUIRE],
[
  _JS_CHECK_INTERNAL([$1], [require])
])
