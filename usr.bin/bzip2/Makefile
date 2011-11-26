# $FreeBSD: src/usr.bin/bzip2/Makefile,v 1.5.2.2 2001/08/02 01:09:06 obrien Exp $
# $DragonFly: src/usr.bin/bzip2/Makefile,v 1.8 2007/08/27 16:50:51 pavalos Exp $

BZ2DIR=	${.CURDIR}/../../contrib/bzip2

.PATH: ${BZ2DIR}

PROG=	bzip2
WARNS?=	2

CFLAGS+= -D_FILE_OFFSET_BITS=64

DPADD=	${LIBBZ2}
LDADD=	-lbz2

NOSHARED?=	YES

LINKS=	${BINDIR}/bzip2 ${BINDIR}/bunzip2
LINKS+=	${BINDIR}/bzip2 ${BINDIR}/bzcat
MLINKS=	bzip2.1 bunzip2.1 bzip2.1 bzcat.1 bzip2.1 bzip2recover.1

.include <bsd.prog.mk>
