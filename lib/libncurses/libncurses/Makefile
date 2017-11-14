# Build standard version of ncurses
LIB?=	private_ncurses
PRIVATELIB=

SHLIBDIR?=	/lib

ZNCURSES=	yes
ZTRACE=		yes
CFLAGS+=	-DTRACE
CFLAGS+=	-DDRAGONFLY_NATIVE

.include "${.CURDIR}/../Makefile.sources"
.include <bsd.lib.mk>
