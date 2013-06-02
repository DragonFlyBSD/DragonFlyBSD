#!/bin/sh
# $FreeBSD: src/tools/regression/lib/msun/test-cexp.t,v 1.1 2011/03/07 03:15:49 das Exp $

cd `dirname $0`

executable=`basename $0 .t`

make $executable 2>&1 > /dev/null

exec ./$executable
