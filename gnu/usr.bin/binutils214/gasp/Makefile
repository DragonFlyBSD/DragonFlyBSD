# $FreeBSD: src/gnu/usr.bin/binutils/gasp/Makefile,v 1.6.2.2 2002/09/01 23:39:14 obrien Exp $
# $DragonFly: src/gnu/usr.bin/binutils214/gasp/Attic/Makefile,v 1.1 2004/02/01 08:53:03 dillon Exp $ 

.include "../Makefile.inc0"

.PATH: ${SRCDIR}/gas

PROG=	gasp
SRCS+=	as.c macro.c sb.c hash.c
WARNS=	2
CFLAGS+= -DBFD_ASSEMBLER -DVERSION=\"${VERSION}\"
CFLAGS+= -I${SRCDIR} -I${SRCDIR}/gas -I${SRCDIR}/gas/config
CFLAGS+= -I${.CURDIR}/../as/${TARGET_ARCH}-freebsd -I${.CURDIR}/../as
CFLAGS+= -I${.OBJDIR}/../libbfd

DPADD=	${RELTOP}/libiberty/libiberty.a
LDADD=	${DPADD}

.include <bsd.prog.mk>
