# $FreeBSD: src/secure/Makefile.inc,v 1.13.2.5 2002/07/03 22:13:19 des Exp $
# $DragonFly: src/secure/Makefile.inc,v 1.3 2004/07/31 19:03:00 geekgod Exp $

DISTRIBUTION?=crypto

TELNETDIR=	${.CURDIR}/../../../crypto/telnet
.if exists(${.OBJDIR}/../../lib/libtelnet)
LIBTELNET=	${.OBJDIR}/../../lib/libtelnet/libtelnet.a
.else
LIBTELNET=	${.CURDIR}/../../lib/libtelnet/libtelnet.a
.endif

.if exists(${.CURDIR}/../../lib/libcrypt/obj)
CRYPTOBJDIR=	${.CURDIR}/../../lib/libcrypt/obj
.else
CRYPTOBJDIR=	${.CURDIR}/../../lib/libcrypt
.endif

.if !defined(MAKE_IDEA) || ${MAKE_IDEA} != YES
CFLAGS+= -DNO_IDEA
.endif
