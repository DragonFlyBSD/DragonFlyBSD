# $NetBSD: Makefile,v 1.2 2003/12/05 13:37:48 lukem Exp $
# $DragonFly: src/lib/libedit/TEST/Makefile,v 1.1 2005/11/13 11:58:30 corecode Exp $

NOMAN=1
PROG=test
CPPFLAGS=-I${.CURDIR}/..
LDADD+=-ledit -ltermcap
DPADD+=${LIBEDIT} ${LIBTERMCAP}

.ifdef DEBUG
CPPFLAGS+=-DDEBUG
.endif

.include <bsd.prog.mk>
