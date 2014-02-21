.PATH: ${.CURDIR}/..

CFLAGS+=-I${.CURDIR}/..
CFLAGS+= -DHAVE_REALLOCF -DHAVE_STRLCPY -DHAVE_GETPROGNAME
CFLAGS+=-DCONF_PATH='"/etc/dma"'
CFLAGS+=-DLIBEXEC_PATH='"/usr/libexec"' -DDMA_VERSION='"v0.9+"'

DPADD=  ${LIBSSL} ${LIBCRYPTO}
LDADD=  -lssl -lcrypto

PROG=	dma
SRCS=	aliases_parse.y aliases_scan.l base64.c conf.c crypto.c
SRCS+=	dma.c dns.c local.c mail.c net.c spool.c util.c
MAN=	dma.8

BINOWN= root
BINGRP= mail
BINMODE=2555

.include <bsd.prog.mk>
