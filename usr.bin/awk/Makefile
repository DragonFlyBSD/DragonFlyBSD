# $FreeBSD: src/usr.bin/awk/Makefile,v 1.9.2.1 2002/06/21 20:12:08 obrien Exp $
# $DragonFly: src/usr.bin/awk/Makefile,v 1.12 2007/08/27 16:50:51 pavalos Exp $

AWKSRC=	${.CURDIR}/../../contrib/awk
.PATH: ${AWKSRC}

PROG=	awk
SRCS=	awkgram.y b.c lex.c lib.c main.c parse.c proctab.c run.c tran.c ytab.h
WARNS?=	2

CFLAGS+= -I. -I${AWKSRC} -DHAS_ISBLANK

DPADD=	${LIBM}
LDADD=	-lm

CLEANFILES= maketab proctab.c ytab.h maketab.nx

ytab.h: awkgram.h
	${LN} -sf ${.ALLSRC} ${.TARGET}

proctab.c: maketab.nx
	./maketab.nx > proctab.c

build-tools: maketab.nx

maketab.nx: ytab.h ${AWKSRC}/maketab.c

.include <bsd.prog.mk>
