#!/bin/sh
#
# $DragonFly: src/usr.bin/wmake/wmake.sh,v 1.1 2003/08/11 00:25:17 dillon Exp $
#
# This script was written by Matt dillon and has been placed in the
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

eval `cd $path; make WMAKE_ARGS="$@" -f Makefile.inc1 wmake`

