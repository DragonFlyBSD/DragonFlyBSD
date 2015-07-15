#!/bin/sh
#
# $DragonFly: src/usr.bin/wmake/wmake.sh,v 1.3 2006/02/11 10:42:12 corecode Exp $
#
# This script was written by Matt Dillon and has been placed in the
# public domain.

path=$PWD
while [ "$path" != "" ]; do
    if [ -f $path/Makefile.inc1 ]; then
	break
    fi
    path=${path%/*}
done
if [ "$path" = "" ]; then
    echo "Unable to locate Makefile.inc through parent dirs"
fi

escaped_args=`echo -n "$@" | sed -e "s/'/\\'/"`
for i in $escaped_args; do
    case $i in
    DESTDIR=*)
	escaped_args="$escaped_args _SHLIBDIRPREFIX=${i#DESTDIR=}"
	;;
    esac
done

eval `cd $path; make WMAKE_ARGS="'$escaped_args'" -f Makefile.inc1 wmake`
