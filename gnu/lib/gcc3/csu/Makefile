# $DragonFly: src/gnu/lib/gcc3/csu/Attic/Makefile,v 1.4 2004/02/02 23:35:09 dillon Exp $

.include "../Makefile.inc"

.PATH: ${GCCDIR}/gcc
SRCS=		crtstuff.c

OBJS=		crtbegin.o crtend.o
SOBJS=		crtbegin.So crtend.So

CFLAGS+=	-I${GCCDIR}/gcc -I${GCCDIR}/gcc/config \
		-I${GCCDIR}/include -I${CCTOOLS_SRC} -I${DFLYH} \
		-I${GCCIR}/gcc/config \
		-I${CCTOOLS_OBJ}

CFLAGS+=	-DIN_GCC \
		-finhibit-size-directive -fno-inline-functions \
		-fno-exceptions -fno-omit-frame-pointer \
		-fno-zero-initialized-in-bss

CRTS_CFLAGS=	-DCRTSTUFFS_O ${PICFLAG}

# crtbegin.c needs either CRT_BEGIN or CRT_END defined, and we can't
# set a default in CFLAGS because we need to switch down below.
#
MKDEPCMD=	CC="${CC}" MKDEP_CPP_OPTS="-M -DCRT_BEGIN" mkdep

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
