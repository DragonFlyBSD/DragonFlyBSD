#!/bin/sh
# $FreeBSD: src/tools/regression/lib/msun/test-exponential.t,v 1.1 2008/01/18 21:46:54 das Exp $

cd `dirname $0`

executable=`basename $0 .t`

make $executable 2>&1 > /dev/null

exec ./$executable
