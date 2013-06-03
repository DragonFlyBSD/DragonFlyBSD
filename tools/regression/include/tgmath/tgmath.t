#!/bin/sh
# $FreeBSD: src/tools/regression/include/tgmath/tgmath.t,v 1.2 2012/11/17 01:53:49 svnexp Exp $

cd `dirname $0`

executable=`basename $0 .t`

make $executable 2>&1 > /dev/null

exec ./$executable
