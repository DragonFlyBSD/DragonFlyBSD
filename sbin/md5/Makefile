#	@(#)Makefile	8.1 (Berkeley) 6/9/93
# $FreeBSD: src/sbin/md5/Makefile,v 1.10 2005/03/09 19:23:04 cperciva Exp $

PROG=	md5

LINKS=	${BINDIR}/md5 ${BINDIR}/rmd160 \
	${BINDIR}/md5 ${BINDIR}/sha1 \
	${BINDIR}/md5 ${BINDIR}/sha256 \
	${BINDIR}/md5 ${BINDIR}/sha512

MLINKS=	md5.1 rmd160.1 \
	md5.1 sha1.1 \
	md5.1 sha256.1 \
	md5.1 sha512.1

CFLAGS+=	${PRIVATELIB_CFLAGS}
LDFLAGS+=	${PRIVATELIB_LDFLAGS}
DPADD=		${LIBCRYPTO}
LDADD=		-lprivate_crypto

.include <bsd.prog.mk>
