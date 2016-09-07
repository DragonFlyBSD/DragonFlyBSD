#	$OpenBSD: Makefile,v 1.1 2003/09/19 17:58:25 otto Exp $

PROG=	dc
SRCS=	dc.c bcode.c inout.c mem.c stack.c
WARNS?=	2

DPADD=		${LIBRECRYPTO}
LDADD=		-lprivate_crypto
LDFLAGS+=	-rpath /lib/priv -L ${_SHLIBDIRPREFIX}/usr/lib/priv
CFLAGS+=	-I${.CURDIR}/../../crypto/libressl/include

.include <bsd.prog.mk>
