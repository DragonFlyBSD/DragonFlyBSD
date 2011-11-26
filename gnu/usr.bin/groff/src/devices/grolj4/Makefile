# $FreeBSD: src/gnu/usr.bin/groff/src/devices/grolj4/Makefile,v 1.5 2005/10/20 10:57:52 ru Exp $
# $DragonFly: src/gnu/usr.bin/groff/src/devices/grolj4/Makefile,v 1.4 2008/03/02 23:02:01 swildner Exp $

PROG_CXX=	grolj4
MAN=		grolj4.1 lj4_font.5
SRCS=		lj4.cpp
DPADD=		${LIBDRIVER} ${LIBGROFF} ${LIBM}
LDADD=		${LIBDRIVER} ${LIBGROFF} -lm
CLEANFILES=	${MAN}

.include <bsd.prog.mk>
