# $NetBSD: Makefile,v 1.2 2003/12/05 13:37:48 lukem Exp $
# $DragonFly: src/lib/libedit/TEST/Makefile,v 1.2 2007/05/05 00:27:40 pavalos Exp $

NOMAN=1
PROG=test
CFLAGS=-I${.CURDIR}/..
LDADD+=-ledit -ltermcap
DPADD+=${LIBEDIT} ${LIBTERMCAP}

.ifdef DEBUG
CFLAGS+=-DDEBUG
.endif

.include <bsd.prog.mk>
