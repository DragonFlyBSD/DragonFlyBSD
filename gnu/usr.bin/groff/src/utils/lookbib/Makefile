# $FreeBSD: src/gnu/usr.bin/groff/src/utils/lookbib/Makefile,v 1.1.2.2 2002/07/19 08:34:12 ru Exp $
# $DragonFly: src/gnu/usr.bin/groff/src/utils/lookbib/Makefile,v 1.2 2003/06/17 04:25:46 dillon Exp $

PROG_CXX=	lookbib
SRCS=		lookbib.cc
DPADD=		${LIBBIB} ${LIBGROFF} ${LIBM}
LDADD=		${LIBBIB} ${LIBGROFF} -lm
CLEANFILES=	${MAN}

.include <bsd.prog.mk>
