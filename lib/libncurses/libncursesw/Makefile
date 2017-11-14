# Build wide version of ncurses
LIB=	private_ncursesw
PRIVATELIB=

SHLIBDIR?=	/lib

ZNCURSES=	yes
ZTRACE=		yes
CFLAGS+=	-DTRACE
CFLAGS+=	-DDRAGONFLY_NATIVE

INCS=		${HEADERS} ${SRCHDRS}
INCSDIR=	${INCLUDEDIR}/priv/ncurses
INCSLINKS=	curses.h ${INCLUDEDIR}/priv/ncurses/ncurses.h

ENABLE_WIDEC=	yes

.include "${.CURDIR}/../Makefile.sources"
.include <bsd.lib.mk>
