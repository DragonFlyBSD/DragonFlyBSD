# $FreeBSD: src/usr.bin/opieinfo/Makefile,v 1.4.2.4 2002/07/15 14:17:08 des Exp $
# $DragonFly: src/usr.bin/opieinfo/Makefile,v 1.3 2004/11/17 19:16:02 dillon Exp $
#
OPIE_DIST?=	${.CURDIR}/../../contrib/opie

PROG=	opieinfo

CFLAGS+=-I${.CURDIR}/../../lib/libopie
CFLAGS+=-I${OPIE_DIST}
CFLAGS+=-DINSECURE_OVERRIDE

DPADD=	${LIBOPIE} ${LIBMD}
LDADD=	-lopie -lmd

BINOWN=	root
BINMODE=4555
.if !defined(NOFSCHG)
INSTALLFLAGS=-fschg
.endif

.PATH:	${OPIE_DIST}

.include <bsd.prog.mk>
