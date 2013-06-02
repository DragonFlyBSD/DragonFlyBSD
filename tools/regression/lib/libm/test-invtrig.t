#!/bin/sh
# $FreeBSD: src/tools/regression/lib/msun/test-invtrig.t,v 1.1 2008/07/31 22:43:38 das Exp $

cd `dirname $0`

executable=`basename $0 .t`

make $executable 2>&1 > /dev/null

exec ./$executable
