#!/bin/sh

srcdir=`dirname $0`
test -z "$srcdir" && srcdir=.

PROJECT=evd
TEST_TYPE=-d
FILE=evd

test $TEST_TYPE $FILE || {
        echo "You must run this script in the top-level $PROJECT directory"
        exit 1
}

AUTORECONF=`which autoreconf`
if test -z $AUTORECONF; then
        echo "*** No autoreconf found ***"
        exit 1
else
        ACLOCAL="${ACLOCAL-aclocal} $ACLOCAL_FLAGS" autoreconf -v --install || exit $?
fi

./configure "$@" && echo "Now type 'make' to compile $PROJECT."
