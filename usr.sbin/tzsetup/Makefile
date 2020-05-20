# $FreeBSD: head/usr.sbin/tzsetup/Makefile 314463 2017-03-01 04:23:53Z ngie $

PROG=	tzsetup
MAN=	tzsetup.8

.if !defined(BOOTSTRAPPING)
CFLAGS+=-DHAVE_DIALOG
DPADD=	${LIBDIALOG} ${LIBM} ${LIBNCURSESW}
LDADD=	-ldialog -lm -lprivate_ncursesw
CFLAGS+=	-I${_SHLIBDIRPREFIX}/usr/include/priv/ncurses
LDFLAGS+=	${PRIVATELIB_LDFLAGS}
.endif

.include <bsd.prog.mk>
