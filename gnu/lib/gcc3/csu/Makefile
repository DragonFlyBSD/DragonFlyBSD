# $DragonFly: src/gnu/lib/gcc3/csu/Attic/Makefile,v 1.3 2004/01/21 21:59:04 joerg Exp $

GCCDIR=	${.CURDIR}/../../../../contrib/gcc3
CCTOOLS_SRC = ${.CURDIR}/../../../usr.bin/cc3/cc_tools
CCTOOLS_OBJ = ${.OBJDIR}/../../../usr.bin/cc3/cc_tools

.PATH: ${GCCDIR}
SRCS=		crtstuff.c

OBJS=		crtbegin.o crtend.o
SOBJS=		crtbegin.So crtend.So
CFLAGS+=	-I${GCCDIR} -I${GCCDIR}/config -I${CCTOOLS_SRC} \
		-I${CCTOOLS_OBJ} -DIN_GCC \
		-finhibit-size-directive -fno-inline-functions \
		-fno-exceptions -fno-omit-frame-pointer \
		-fno-zero-initialized-in-bss

CRTS_CFLAGS=	-DCRTSTUFFS_O ${PICFLAG}

all: ${OBJS} ${SOBJS}

CLEANFILES+=	${OBJS} ${SOBJS}

.if exists(${CCTOOLS_OBJ})
TCONFIG_H = ${CCTOOLS_OBJ}/tconfig.h
.else
TCONFIG_H = ${CCTOOLS_SRC}/tconfig.h
.endif

crtbegin.o crtbegin.So crtend.o crtend.So: ${SRCS} ${TCONFIG_H}

crtbegin.o:
	${CC} ${CFLAGS} -g0 -DCRT_BEGIN \
		-c -o ${.TARGET} ${UGLYHACK} ${.ALLSRC:N*.h}

crtbegin.So:
	${CC} ${CFLAGS} -g0 -DCRT_BEGIN ${CRTS_CFLAGS} \
		-c -o ${.TARGET} ${UGLYHACK} ${.ALLSRC:N*.h}

crtend.o:
	${CC} ${CFLAGS} -g0 -DCRT_END \
		-c -o ${.TARGET} ${UGLYHACK} ${.ALLSRC:N*.h}

crtend.So:
	${CC} ${CFLAGS} -g0 -DCRT_END ${CRTS_CFLAGS} \
		-c -o ${.TARGET} ${UGLYHACK} ${.ALLSRC:N*.h}

${TCONFIG_H}: ${CCTOOLS_SRC}/Makefile
	cd ${CCTOOLS_SRC} && make tconfig.h

beforedepend: ${TCONFIG_H}

realinstall:
.for file in ${OBJS} ${SOBJS}
	${INSTALL} -o ${LIBOWN} -g ${LIBGRP} -m ${LIBMODE} \
	    ${file} ${DESTDIR}${GCCLIBDIR}/${file:S/.So$/S.o/}
.endfor

.include <bsd.lib.mk>
