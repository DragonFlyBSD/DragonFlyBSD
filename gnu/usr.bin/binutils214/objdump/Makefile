# $FreeBSD: src/gnu/usr.bin/binutils/objdump/Makefile,v 1.4.2.2 2002/09/01 23:39:17 obrien Exp $
# $DragonFly: src/gnu/usr.bin/binutils214/objdump/Attic/Makefile,v 1.1 2004/02/01 08:53:05 dillon Exp $

.include "../Makefile.inc0"

.PATH: ${SRCDIR}/binutils ${SRCDIR}/binutils/doc

PROG=	objdump
SRCS=	objdump.c prdbg.c
WARNS=	2
CFLAGS+= -I${.CURDIR}/${RELTOP}/libbinutils
CFLAGS+= -I${SRCDIR}/binutils
CFLAGS+= -I${SRCDIR}/bfd -I${.OBJDIR}/../libbfd
CFLAGS+= -DBFD_VERSION_STRING=\"${VERSION}\"
DPADD=	${RELTOP}/libbinutils/libbinutils.a
DPADD+=	${RELTOP}/libopcodes/libopcodes.a
DPADD+=	${RELTOP}/libbfd/libbfd.a
DPADD+=	${RELTOP}/libiberty/libiberty.a
LDADD=	${DPADD}

.include <bsd.prog.mk>
