# $DragonFly: src/usr.sbin/installer/libaura/Makefile,v 1.2 2008/03/25 22:55:38 dave Exp $

BSDINS_DIR=${.CURDIR}/../../../contrib/bsdinstaller-1.1.6
BSDINS_SRC=${BSDINS_DIR}/src
CONTRIBDIR= ${BSDINS_SRC}/lib/libaura
.PATH:	${CONTRIBDIR}

CFLAGS+=-I${CONTRIBDIR} -g

LIB=    aura

SRCS+=  buffer.c dict.c fspred.c mem.c popen.c

INTERNALLIB=            true

.include <bsd.lib.mk>

