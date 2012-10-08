GCC_NO_PATH=	# defined
LOCAL_CONFIG=	# defined
.include "../Makefile.inc"
.PATH: ${GCCDIR}/libdecnumber
.PATH: ${GCCDIR}/libdecnumber/dpd

LIB=		decnumber
INTERNALLIB=	YES

CFLAGS+=	-DBYTESLONG=${BYTESLONG}

SRCS=	decContext.c \
	decNumber.c \
	decimal32.c \
	decimal64.c \
	decimal128.c

#generated
SRCS+=	gstdint.h

gstdint.h:
	echo "#include <stdint.h>" > ${.TARGET}

CLEANFILES+=	gstdint.h

.include <bsd.lib.mk>
