.include "../Makefile.inc"

LIB=	decnumber
INTERNALLIB=	YES

SRCS=	decNumber.c decContext.c decimal32.c decimal64.c decimal128.c

GCC_NO_PATH=	# defined

CFLAGS+=	-I${.OBJDIR}

beforedepend: gstdint.h
gstdint.h:
	echo "#include <stdint.h>" > ${.TARGET}

CLEANFILES+=	gstdint.h

.include <bsd.lib.mk>

.PATH:	${GCCDIR}/libdecnumber ${GCCDIR}/libdecnumber/dpd
