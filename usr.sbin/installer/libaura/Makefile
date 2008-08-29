# $DragonFly: src/usr.sbin/installer/libaura/Makefile,v 1.3 2008/08/29 22:51:56 swildner Exp $

BSDINS_DIR=${.CURDIR}/../../../contrib/bsdinstaller-1.1.6
BSDINS_SRC=${BSDINS_DIR}/src
CONTRIBDIR= ${BSDINS_SRC}/lib/libaura
.PATH:	${CONTRIBDIR}

CFLAGS+=-I${CONTRIBDIR}

LIB=    aura

SRCS=  buffer.c dict.c fspred.c mem.c popen.c

INTERNALLIB=	true

.include <bsd.lib.mk>
