# $FreeBSD: head/usr.sbin/ckdist/Makefile 314659 2017-03-04 11:38:03Z ngie $

.PATH: ${.CURDIR}/../../usr.bin/cksum

PROG=	ckdist
SRCS=   ckdist.c crc.c


CFLAGS+=	${PRIVATELIB_CFLAGS}
DPADD=		${LIBCRYPTO}
LDADD=		-lprivate_crypto
LDFLAGS+=	${PRIVATELIB_LDFLAGS}


.include <bsd.prog.mk>
