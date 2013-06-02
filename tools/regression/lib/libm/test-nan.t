#!/bin/sh
# $FreeBSD: src/tools/regression/lib/msun/test-nan.t,v 1.1 2007/12/16 21:19:51 das Exp $

cd `dirname $0`

executable=`basename $0 .t`

make $executable 2>&1 > /dev/null

exec ./$executable
