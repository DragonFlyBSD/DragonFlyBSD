# $FreeBSD: src/usr.sbin/cdcontrol/Makefile,v 1.2.2.2 2002/03/15 22:34:25 mikeh Exp $

PROG= cdcontrol

DPADD=	${LIBEDIT} ${LIBTERMCAP}
LDADD=	-lprivate_edit -ltermcap
LDFLAGS+=	-rpath /lib/priv -L ${_SHLIBDIRPREFIX}/usr/lib/priv

.include <bsd.prog.mk>
