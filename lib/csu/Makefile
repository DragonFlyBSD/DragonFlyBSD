CSUDIR=	${.CURDIR}/${MACHINE_ARCH}

.include "${.CURDIR}/${MACHINE_ARCH}/Makefile.csu"

OBJS+=	${SRCS:M*.[cS]:R:C/$/.o/g}

all: ${OBJS}

realinstall:
	${INSTALL} -o ${LIBOWN} -g ${LIBGRP} -m ${LIBMODE} \
	    ${OBJS} ${DESTDIR}${LIBDIR}


.include <bsd.lib.mk>
