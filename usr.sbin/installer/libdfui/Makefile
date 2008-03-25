# $DragonFly: src/usr.sbin/installer/libdfui/Makefile,v 1.2 2008/03/25 22:55:38 dave Exp $

BSDINS_DIR=${.CURDIR}/../../../contrib/bsdinstaller-1.1.6
BSDINS_SRC=${BSDINS_DIR}/src

CONTRIBDIR=${BSDINS_SRC}/lib/libdfui
.PATH:	${CONTRIBDIR}

CFLAGS+=-I${CONTRIBDIR} -I. -g

LIB=    dfui

SRCS+= conn_caps.c conn_npipe.c conn_tcp.c connection.c decode.c \
	dump.c encode.c form.c lang.c system.c aura.h

# This madness is because the installer sources reference aura includes
# as <aura/mem.h> rather than <libaura/mem.h>
aura.h:
	${LN} -sf ${BSDINS_SRC}/lib/libaura aura
	/usr/bin/touch aura.h

CLEANFILES+=	aura.h aura
INTERNALLIB=            true

.include <bsd.lib.mk>

