# $FreeBSD: src/usr.bin/units/Makefile,v 1.5.2.1 2002/02/18 16:16:57 ru Exp $

PROG=	units
FILES=	units.lib
FILESDIR=	${SHAREDIR}/misc

LDADD+=	-lprivate_edit -lprivate_termcap
DPADD+=	${LIBEDIT} ${LIBTERMCAP}
LDFLAGS+=	-rpath /lib/priv -L ${_SHLIBDIRPREFIX}/usr/lib/priv

.include <bsd.prog.mk>
