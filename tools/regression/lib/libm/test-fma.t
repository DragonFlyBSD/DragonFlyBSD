#!/bin/sh
# $FreeBSD: src/tools/regression/lib/msun/test-fma.t,v 1.1 2008/04/03 06:15:58 das Exp $

cd `dirname $0`

executable=`basename $0 .t`

make $executable 2>&1 > /dev/null

exec ./$executable
