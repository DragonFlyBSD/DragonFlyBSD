#!/bin/sh
# $FreeBSD: src/tools/regression/lib/msun/test-logarithm.t,v 1.1 2010/12/05 22:18:35 das Exp $

cd `dirname $0`

executable=`basename $0 .t`

make $executable 2>&1 > /dev/null

exec ./$executable
