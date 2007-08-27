#	@(#)Makefile	8.1 (Berkeley) 6/6/93
# $FreeBSD: src/usr.bin/gcore/Makefile,v 1.6 1999/11/14 13:54:43 marcel Exp $
# $DragonFly: src/usr.bin/gcore/Makefile,v 1.3 2007/08/27 16:50:54 pavalos Exp $

PROG=	gcore
SRCS=	elfcore.c gcore.c
DPADD=	${LIBKVM}
LDADD=	-lkvm
WARNS?=	2

.if	${MACHINE_ARCH} != "sparc"
SRCS+=	md-nop.c
.else
SRCS+=	md-${MACHINE_ARCH}.c
.endif

.include <bsd.prog.mk>
