.include "../Makefile.inc"

LIB=	decnumber
INTERNALLIB=	YES

SRCS=	decNumber.c decContext.c decimal32.c decimal64.c decimal128.c

.PATH:	${BASEDIR}/libdecnumber ${BASEDIR}/libdecnumber/dpd

CFLAGS+=	-I${.CURDIR} -I${.OBJDIR} -I${BASEDIR}/libdecnumber

beforedepend: gstdint.h
gstdint.h:
	echo "#include <stdint.h>" > ${.TARGET}

CLEANFILES+=	gstdint.h

.include <bsd.lib.mk>
