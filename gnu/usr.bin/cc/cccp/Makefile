# $FreeBSD: src/gnu/usr.bin/cc/cccp/Makefile,v 1.16.2.3 2001/08/01 18:48:34 obrien Exp $
# $DragonFly: src/gnu/usr.bin/cc/cccp/Attic/Makefile,v 1.6 2004/04/07 12:57:31 joerg Exp $

.include "../Makefile.inc"

.PATH: ${GCCDIR}

PROG=	cpp0
SRCS=	cccp.c cexp.y obstack.c prefix.c version.c
YFLAGS=
NOSHARED?=yes
MAN=	cccp.1
LINKS=	${BINDIR}/cpp0 ${BINDIR}/cpp
MLINKS=	cccp.1 cpp.1

# Remove dangerous optimizer settings for the system compiler
CFLAGS:= ${CFLAGS:N-O*:N-march=*}

.include <bsd.prog.mk>
