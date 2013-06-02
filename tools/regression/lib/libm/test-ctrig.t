#!/bin/sh
# $FreeBSD: src/tools/regression/lib/msun/test-ctrig.t,v 1.1 2011/10/21 06:34:38 das Exp $

cd `dirname $0`

executable=`basename $0 .t`

make $executable 2>&1 > /dev/null

exec ./$executable
