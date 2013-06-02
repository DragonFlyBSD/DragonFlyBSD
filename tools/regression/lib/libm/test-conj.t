#!/bin/sh
# $FreeBSD: src/tools/regression/lib/msun/test-conj.t,v 1.1 2009/01/31 18:31:57 das Exp $

cd `dirname $0`

executable=`basename $0 .t`

make $executable 2>&1 > /dev/null

exec ./$executable
