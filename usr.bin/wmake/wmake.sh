#!/bin/sh
#
# $DragonFly: src/usr.bin/wmake/wmake.sh,v 1.2 2005/09/23 00:31:37 corecode Exp $
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

escaped_args=`echo "$@" | sed -e "s/'/\\'/"`

eval `cd $path; make WMAKE_ARGS="'$escaped_args'" -f Makefile.inc1 wmake`
