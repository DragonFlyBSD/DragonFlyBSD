# $FreeBSD: src/gnu/usr.bin/groff/src/devices/grodvi/Makefile,v 1.1.2.2 2002/07/19 08:34:07 ru Exp $
# $DragonFly: src/gnu/usr.bin/groff/src/devices/grodvi/Makefile,v 1.2 2003/06/17 04:25:46 dillon Exp $

PROG_CXX=	grodvi
SRCS=		dvi.cc
DPADD=		${LIBDRIVER} ${LIBGROFF} ${LIBM}
LDADD=		${LIBDRIVER} ${LIBGROFF} -lm
CLEANFILES=	${MAN}

.include <bsd.prog.mk>
