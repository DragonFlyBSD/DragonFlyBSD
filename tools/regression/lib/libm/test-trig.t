#!/bin/sh
# $FreeBSD: src/tools/regression/lib/msun/test-trig.t,v 1.1 2008/02/18 02:00:16 das Exp $

cd `dirname $0`

executable=`basename $0 .t`

make $executable 2>&1 > /dev/null

exec ./$executable
