# $FreeBSD: src/sbin/ping6/Makefile,v 1.1.2.4 2002/03/08 09:18:59 ume Exp $
# $DragonFly: src/sbin/ping6/Makefile,v 1.5 2008/09/04 09:08:22 hasso Exp $

PROG=	ping6
MAN=	ping6.8

CFLAGS+=-DINET6 -DIPSEC -DUSE_RFC3542

BINOWN=	root
BINMODE=4555

LDADD=	-lipsec -lm -lmd
DPADD= ${LIBIPSEC} ${LIBM} ${LIBMD}

# kame scopeid hack
CFLAGS+=-DKAME_SCOPEID

.include <bsd.prog.mk>
