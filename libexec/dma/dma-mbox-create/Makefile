.PATH: ${.CURDIR}/..

CFLAGS+=-I${.CURDIR}/..
CFLAGS+= -DHAVE_REALLOCF -DHAVE_STRLCPY -DHAVE_GETPROGNAME
CFLAGS+=-DCONF_PATH='"/etc/dma"'
CFLAGS+=-DLIBEXEC_PATH='"/usr/libexec"' -DDMA_VERSION='"v0.13+"'
CFLAGS+=-I${.CURDIR}/../../../crypto/libressl/include

PROG=	dma-mbox-create
NOMAN=

BINOWN=	root
BINGRP=	mail
BINMODE=4554

.include <bsd.prog.mk>
