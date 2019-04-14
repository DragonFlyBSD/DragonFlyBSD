# $FreeBSD: head/usr.bin/unzip/Makefile 275042 2014-11-25 14:29:10Z bapt $

PROG=	unzip
LDADD=	-larchive
DPADD=	${LIBARCHIVE}

# for static libarchive.a
DPADD+=		${LIBBZ2} ${LIBZ} ${{LIBCRYPTO}
LDADD+=		-lbz2 -lz -lprivate_crypto
LDFLAGS+=	${PRIVATELIB_LDFLAGS}

.include <bsd.prog.mk>
