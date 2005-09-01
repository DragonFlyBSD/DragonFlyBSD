# $FreeBSD: src/gnu/usr.bin/groff/src/devices/grohtml/Makefile,v 1.7 2004/12/21 08:47:00 ru Exp $
# $DragonFly: src/gnu/usr.bin/groff/src/devices/grohtml/Makefile,v 1.3 2005/09/01 20:47:13 asmodai Exp $

PROG_CXX=	post-grohtml
SRCS=		post-html.cpp html-table.cpp html-text.cpp output.cpp
DPADD=		${LIBDRIVER} ${LIBGROFF} ${LIBM}
LDADD=		${LIBDRIVER} ${LIBGROFF} -lm
NOMAN=		YES

.include <bsd.prog.mk>
