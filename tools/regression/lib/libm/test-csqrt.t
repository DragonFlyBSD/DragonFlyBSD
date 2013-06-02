#!/bin/sh
# $FreeBSD: src/tools/regression/lib/msun/test-csqrt.t,v 1.1 2007/12/15 09:16:26 das Exp $

cd `dirname $0`

executable=`basename $0 .t`

make $executable 2>&1 > /dev/null

exec ./$executable
