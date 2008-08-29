# $DragonFly: src/usr.sbin/installer/libdfui/Makefile,v 1.3 2008/08/29 22:51:56 swildner Exp $

BSDINS_DIR=${.CURDIR}/../../../contrib/bsdinstaller-1.1.6
BSDINS_SRC=${BSDINS_DIR}/src

CONTRIBDIR=${BSDINS_SRC}/lib/libdfui
.PATH:	${CONTRIBDIR}

CFLAGS+=-I${BSDINS_SRC}/lib

LIB=    dfui

SRCS= conn_caps.c conn_npipe.c conn_tcp.c connection.c decode.c \
	dump.c encode.c form.c lang.c system.c

CLEANFILES+=	aura
INTERNALLIB=	true

.include <bsd.lib.mk>
