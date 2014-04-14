# $FreeBSD: src/usr.bin/units/Makefile,v 1.5.2.1 2002/02/18 16:16:57 ru Exp $
# $DragonFly: src/usr.bin/units/Makefile,v 1.4 2007/08/27 16:51:00 pavalos Exp $

PROG=	units
FILES=	units.lib
FILESDIR=	${SHAREDIR}/misc

LDADD+=	-ledit -ltermcap
DPADD+=	${LIBEDIT} ${LIBTERMCAP}

.include <bsd.prog.mk>
