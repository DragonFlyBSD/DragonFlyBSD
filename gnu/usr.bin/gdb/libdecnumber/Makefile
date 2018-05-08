.include "../Makefile.inc"

LIB=	decnumber
INTERNALLIB=	YES

# for now do not let the use of -flto (check -Wlto-type-mismatch !)
.if ${CFLAGS:M-flto}
CFLAGS+=	-fno-lto
.endif

SRCS=	decNumber.c decContext.c decimal32.c decimal64.c decimal128.c

.PATH:	${BASEDIR}/libdecnumber ${BASEDIR}/libdecnumber/dpd

CFLAGS+=	-I${.CURDIR} -I${.OBJDIR} -I${BASEDIR}/libdecnumber

.include <bsd.lib.mk>
