# $FreeBSD: src/gnu/usr.bin/binutils/nm/Makefile,v 1.4.2.2 2002/09/01 23:39:17 obrien Exp $
# $DragonFly: src/gnu/usr.bin/binutils214/nm/Attic/Makefile,v 1.1 2004/02/01 08:53:04 dillon Exp $

.include "../Makefile.inc0"

.PATH: ${SRCDIR}/binutils ${SRCDIR}/binutils/doc

PROG=	nm
SRCS=	nm.c
WARNS=	2
CFLAGS+= -I${.CURDIR}/${RELTOP}/libbinutils
CFLAGS+= -I${SRCDIR}/binutils
CFLAGS+= -I${SRCDIR}/bfd -I${.OBJDIR}/../libbfd
DPADD=	${RELTOP}/libbinutils/libbinutils.a
DPADD+=	${RELTOP}/libbfd/libbfd.a
DPADD+=	${RELTOP}/libiberty/libiberty.a
LDADD=	${DPADD}

.include <bsd.prog.mk>
