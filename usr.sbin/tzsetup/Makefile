# $FreeBSD: src/usr.sbin/tzsetup/Makefile,v 1.10.2.1 2001/04/25 12:11:07 ru Exp $

PROG=	tzsetup
MAN=	tzsetup.8

DPADD=	${LIBDIALOG} ${LIBM} ${LIBNCURSESW}
LDADD=	-ldialog -lm -lprivate_ncursesw
LDFLAGS+=	-rpath /lib/priv -L ${_SHLIBDIRPREFIX}/usr/lib/priv

.include <bsd.prog.mk>
