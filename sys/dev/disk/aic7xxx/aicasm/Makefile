# $Id: //depot/aic7xxx/freebsd/dev/aic7xxx/aicasm/Makefile#2 $
#
# $FreeBSD: src/sys/dev/aic7xxx/aicasm/Makefile,v 1.8.2.5 2002/09/27 15:46:28 gibbs Exp $

PROG=	aicasm

CSRCS=	aicasm.c aicasm_symbol.c
YSRCS=	aicasm_gram.y aicasm_macro_gram.y
LSRCS=	aicasm_scan.l aicasm_macro_scan.l

GENHDRS=	aicasm_gram.h aicasm_macro_gram.h
.ORDER: aicasm_gram.c aicasm_macro_gram.c aicasm_scan.c aicasm_macro_scan.c

SRCS=		${GENHDRS} ${CSRCS} ${YSRCS} ${LSRCS}
SRCS+=		sys/types.h ctype.h
CLEANFILES+=	${GENHDRS} ${YSRCS:R:C/(.*)/\1.output/g}
CLEANFILES+=	ctype.h
CLEANDIRS+=	sys
DPADD+=		${LIBL}
LDADD+=		-ll
WARNS?=		6

# Correct path for kernel builds
# Don't rely on the kernel's .depend file
.ifdef MAKESRCPATH
.PATH: ${MAKESRCPATH}
DEPENDFILE=
.endif

CFLAGS+= -I. -I${.CURDIR}
.ifdef MAKESRCPATH
CFLAGS+= -I${MAKESRCPATH}
.endif
NOMAN=	noman
YFLAGS= -b ${.TARGET:R} ${.TARGET:M*macro*:S/$(.TARGET)/-p mm/} -d
LFLAGS+= ${.TARGET:M*macro*:S/$(.TARGET)/-Pmm/}

.ifdef AICASM_DEBUG
CFLAGS+= -DDEBUG -g
YFLAGS+= -t -v
LFLAGS+= -d
.endif

sys/types.h: /usr/include/sys/types.h
	mkdir -p sys
	cp ${.ALLSRC} ${.TARGET}

ctype.h: /usr/include/ctype.h
	cp ${.ALLSRC} ${.TARGET}

# aicasm is used in place, it is not installed anywhere
#
install:

.include <bsd.hostprog.mk>
