# $FreeBSD: src/gnu/usr.bin/man/manpath/Makefile,v 1.22.2.3 2002/02/01 09:00:02 ru Exp $

PROG=	manpath

DPADD=	${LIBMAN}
LDADD=	${LIBMAN}

CFLAGS+= -DMAIN -DDO_UNCOMPRESS
CFLAGS+= -I${.CURDIR}/../lib -I${.OBJDIR}/../lib

.include <bsd.prog.mk>
