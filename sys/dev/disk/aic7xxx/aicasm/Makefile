# $Id: //depot/aic7xxx/freebsd/dev/aic7xxx/aicasm/Makefile#2 $
#
# $FreeBSD: src/sys/dev/aic7xxx/aicasm/Makefile,v 1.8.2.5 2002/09/27 15:46:28 gibbs Exp $
# $DragonFly: src/sys/dev/disk/aic7xxx/aicasm/Makefile,v 1.4 2003/11/14 22:58:31 dillon Exp $

PROG=	aicasm

CSRCS=	aicasm.c aicasm_symbol.c
YSRCS=	aicasm_gram.y aicasm_macro_gram.y
LSRCS=	aicasm_scan.l aicasm_macro_scan.l

GENHDRS=	aicasm_gram.h aicasm_macro_gram.h

SRCS=	${GENHDRS} ${CSRCS} ${YSRCS} ${LSRCS}
CLEANFILES+= ${GENHDRS} ${YSRCS:R:C/(.*)/\1.output/g}
DPADD+=	${LIBL}
LDADD+=	-ll

# Correct path for kernel builds
# Don't rely on the kernel's .depend file
.ifdef MAKESRCPATH
.PATH: ${MAKESRCPATH}
DEPENDFILE=
.endif

CFLAGS+= -nostdinc -I/usr/include -I. -I${.CURDIR}
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

# aicasm is used in place, it is not installed anywhere
#
install:

.include <bsd.prog.mk>
