# $FreeBSD: src/usr.sbin/pkg_install/sign/Makefile,v 1.8 2004/06/29 19:06:42 eik Exp $
# $OpenBSD: Makefile.bsd-wrapper,v 1.2 1999/10/07 16:30:32 espie Exp $
# $DragonFly: src/usr.sbin/pkg_install/sign/Attic/Makefile,v 1.3 2004/07/30 04:46:13 dillon Exp $

PROG=	pkg_sign
LINKS=	${BINDIR}/pkg_sign ${BINDIR}/pkg_check
MLINKS=	pkg_sign.1 pkg_check.1
SRCS=	main.c check.c common.c gzip.c pgp_check.c pgp_sign.c \
	sha1.c sign.c stand.c x509.c

CFLAGS+= ${DEBUG} -I${.CURDIR}/../lib

DISTRIBUTION= crypto
DPADD=	${LIBINSTALL} ${LIBMD} ${LIBCRYPTO}
LDADD=	${LIBINSTALL} -lmd -lcrypto

.include <bsd.prog.mk>

