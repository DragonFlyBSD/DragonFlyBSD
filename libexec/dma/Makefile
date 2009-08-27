# $DragonFly: src/libexec/dma/Makefile,v 1.5 2008/09/19 00:36:57 corecode Exp $
#

CFLAGS+= -I${.CURDIR}

DPADD=  ${LIBSSL} ${LIBCRYPTO}
LDADD=  -lssl -lcrypto

PROG=	dma
SRCS=	aliases_parse.y aliases_scan.l base64.c conf.c crypto.c
SRCS+=	dma.c local.c mail.c net.c spool.c util.c 
MAN=	dma.8

BINOWN= root
BINGRP= mail
BINMODE=2555
WARNS?=	6

.include <bsd.prog.mk>
