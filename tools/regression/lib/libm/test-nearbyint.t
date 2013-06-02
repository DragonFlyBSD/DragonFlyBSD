#!/bin/sh
# $FreeBSD: src/tools/regression/lib/msun/test-nearbyint.t,v 1.1 2010/12/03 00:44:31 das Exp $

cd `dirname $0`

executable=`basename $0 .t`

make $executable 2>&1 > /dev/null

exec ./$executable
