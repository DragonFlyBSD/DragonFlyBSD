GCC_NO_PATH=	yes
GCC_NO_LIBS=	yes
LOCAL_CONFIG=	yes
.include "../Makefile.inc"
.PATH: ${GCCDIR}/libdecnumber
.PATH: ${GCCDIR}/libdecnumber/dpd

LIB=		decnumber
INTERNALLIB=	YES

SRCS=	decContext.c \
	decNumber.c \
	decimal32.c \
	decimal64.c \
	decimal128.c

gstdint.h:
	echo "#include <stdint.h>" > ${.TARGET}

depend all: gstdint.h

CLEANFILES+=	gstdint.h

.include <bsd.lib.mk>
