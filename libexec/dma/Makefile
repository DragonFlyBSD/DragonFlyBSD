# $DragonFly: src/libexec/dma/Makefile,v 1.4 2008/09/16 17:57:22 matthias Exp $
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
BINMODE=4555
WARNS?=	1

.include <bsd.prog.mk>
