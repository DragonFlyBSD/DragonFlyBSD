#!/bin/sh
# $FreeBSD: src/tools/regression/bin/sh/regress.t,v 1.3 2010/10/15 20:01:35 jilles Exp $

export SH="${SH:-sh}"

cd `dirname $0`

${SH} regress.sh
