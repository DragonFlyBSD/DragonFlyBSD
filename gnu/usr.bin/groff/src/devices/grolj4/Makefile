# $FreeBSD: src/gnu/usr.bin/groff/src/devices/grolj4/Makefile,v 1.1.2.2 2002/07/19 08:34:08 ru Exp $
# $DragonFly: src/gnu/usr.bin/groff/src/devices/grolj4/Makefile,v 1.2 2003/06/17 04:25:46 dillon Exp $

PROG_CXX=	grolj4
SRCS=		lj4.cc
DPADD=		${LIBDRIVER} ${LIBGROFF} ${LIBM}
LDADD=		${LIBDRIVER} ${LIBGROFF} -lm
CLEANFILES=	${MAN}

.include <bsd.prog.mk>
