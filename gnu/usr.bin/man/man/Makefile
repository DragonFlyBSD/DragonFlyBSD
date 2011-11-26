# $FreeBSD: src/gnu/usr.bin/man/man/Makefile,v 1.27.2.4 2002/07/17 13:25:17 ru Exp $
#

PROG=	man
SRCS=	man.c manpath.c glob.c
BINOWN= man
BINMODE=4555
.if !defined(NOFSCHG)
INSTALLFLAGS=	-fschg
.endif

CFLAGS+= -I${LIBDESTDIR}
DPADD=	${LIBMAN}
LDADD=	${LIBMAN}

DPADD+=	${LIBZ}
LDADD+=	-lz

CFLAGS+= -I${.CURDIR}/../lib
CFLAGS+= -DCATMODE=0644

.PATH:	${.CURDIR}/../manpath

.include <bsd.prog.mk>
