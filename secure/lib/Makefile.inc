# $FreeBSD: src/secure/lib/Makefile.inc,v 1.7 1999/08/28 01:30:18 peter Exp $
# $DragonFly: src/secure/lib/Makefile.inc,v 1.2 2003/06/17 04:27:48 dillon Exp $

.include "${.CURDIR}/../../Makefile.inc"
.if exists(${.CURDIR}/../../../lib/Makefile.inc)
.include "${.CURDIR}/../../../lib/Makefile.inc"
.endif
