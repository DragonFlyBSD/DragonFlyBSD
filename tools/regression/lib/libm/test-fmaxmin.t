#!/bin/sh
# $FreeBSD: src/tools/regression/lib/msun/test-fmaxmin.t,v 1.1 2008/07/03 23:06:06 das Exp $

cd `dirname $0`

executable=`basename $0 .t`

make $executable 2>&1 > /dev/null

exec ./$executable
