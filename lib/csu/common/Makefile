# $DragonFly: src/lib/csu/common/Attic/Makefile,v 1.2 2005/10/19 16:54:25 joerg Exp $

.if exists(${.CURDIR}/../${MACHINE_ARCH}/Makefile.inc)
.include "${.CURDIR}/../${MACHINE_ARCH}/Makefile.inc"
.endif

.if !defined(ELFSIZE)
ELFSIZE=	32
.endif

CFLAGS+=	-DPIC -DDYNAMIC -DELFSIZE=${ELFSIZE}
CFLAGS+=	-I${.CURDIR}
CFLAGS+=	-DDWARF2_EH
CFLAGS+=	-DJCR
CFLAGS+=	-DDSO_HANDLE
CFLAGS+=	-fPIC

# crtbrand.c is included by the architecture specific crt1.c
SRCS+=		crtbegin.c crtend.c
SRCS+=		crtbeginS.c crtendS.c
OBJS=		${SRCS:R:S/$/.o/g}

CLEANFILES+=	${OBJS}

all: ${OBJS}

crtbegin.o: crtbegin.c
crtbeginS.o: crtbeginS.c
crtend.o: crtend.c
crtendS.o: crtendS.c

realinstall:
	${INSTALL} -o ${LIBOWN} -g ${LIBGRP} -m ${LIBMODE} \
	    ${OBJS} ${DESTDIR}${LIBDIR}

.include <bsd.lib.mk>
