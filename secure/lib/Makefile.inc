# $FreeBSD: src/secure/lib/Makefile.inc,v 1.7 1999/08/28 01:30:18 peter Exp $

.include "${.CURDIR}/../../Makefile.inc"
.if exists(${.CURDIR}/../../../lib/Makefile.inc)
.include "${.CURDIR}/../../../lib/Makefile.inc"
.endif
