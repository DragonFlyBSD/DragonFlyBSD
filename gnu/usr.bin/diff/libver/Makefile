.include 	"../Makefile.inc"

LIB=		ver
INTERNALLIB=	true

CFLAGS+=	-I${.CURDIR} -I${.CURDIR}/../libdiffutils
CFLAGS+=	-DHAVE_CONFIG_H

SRCS=		version.c

.include <bsd.lib.mk>
