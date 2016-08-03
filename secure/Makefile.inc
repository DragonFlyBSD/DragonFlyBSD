# $FreeBSD: src/secure/Makefile.inc,v 1.13.2.5 2002/07/03 22:13:19 des Exp $
# $DragonFly: src/secure/Makefile.inc,v 1.4 2005/09/06 18:55:25 dillon Exp $

.if exists(${.CURDIR}/../../lib/libcrypt/obj)
CRYPTOBJDIR=	${.CURDIR}/../../lib/libcrypt/obj
.else
CRYPTOBJDIR=	${.CURDIR}/../../lib/libcrypt
.endif
