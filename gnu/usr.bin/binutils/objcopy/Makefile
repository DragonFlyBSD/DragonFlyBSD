# $FreeBSD: src/gnu/usr.bin/binutils/objcopy/Makefile,v 1.4.2.2 2002/09/01 23:39:17 obrien Exp $
# $DragonFly: src/gnu/usr.bin/binutils/objcopy/Attic/Makefile,v 1.2 2003/06/17 04:25:45 dillon Exp $

.include "../Makefile.inc0"

.PATH: ${SRCDIR}/binutils ${SRCDIR}/binutils/doc

PROG=	objcopy
SRCS=	objcopy.c not-strip.c
WARNS=	2
CFLAGS+= -I${.CURDIR}/${RELTOP}/libbinutils
CFLAGS+= -I${SRCDIR}/binutils
DPADD=	${RELTOP}/libbinutils/libbinutils.a
DPADD+=	${RELTOP}/libbfd/libbfd.a
DPADD+=	${RELTOP}/libiberty/libiberty.a
LDADD=	${DPADD}

.include <bsd.prog.mk>
