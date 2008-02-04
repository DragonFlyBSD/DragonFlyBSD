# $DragonFly: src/libexec/dma/Makefile,v 1.3 2008/02/04 08:58:54 matthias Exp $
#

CFLAGS+= -DHAVE_CRYPTO
CFLAGS+= -I${.CURDIR}

DPADD=  ${LIBSSL} ${LIBCRYPTO}
LDADD=  -lssl -lcrypto

PROG=	dma
SRCS=	base64.c conf.c crypto.c net.c dma.c aliases_scan.l aliases_parse.y
MAN=	dma.8

BINOWN= root
BINGRP= mail
BINMODE=2555
WARNS?=	1

.include <bsd.prog.mk>
