# Build special version of ncurses for tic(1)
LIB=	tic

# keep at low warns, hostlib
WARNS=	1

ZTIC=	yes
CFLAGS+=	-DDRAGONFLY_NATIVE

ENABLE_WIDEC=	yes

.include "${.CURDIR}/../Makefile.sources"
.include <bsd.hostlib.mk>
