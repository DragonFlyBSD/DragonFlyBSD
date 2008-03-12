# $DragonFly: src/usr.sbin/installer/libaura/Makefile,v 1.1 2008/03/12 01:34:49 dave Exp $

BSDINS_DIR=${.CURDIR}/../../../contrib/bsdinstaller-1.1.6
BSDINS_SRC=${BSDINS_DIR}/src
.PATH:	${BSDINS_SRC}/lib/libaura

CFLAGS+=-I${BSDINS_SRC}/lib/libaura

LIB=    aura

SRCS+=  buffer.c dict.c fspred.c mem.c popen.c

INTERNALLIB=            true

.include <bsd.lib.mk>

