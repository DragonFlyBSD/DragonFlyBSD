# $FreeBSD: src/usr.bin/awk/Makefile,v 1.9.2.1 2002/06/21 20:12:08 obrien Exp $
# $DragonFly: src/usr.bin/awk/Makefile,v 1.6 2004/04/18 00:15:35 drhodus Exp $

AWKSRC=	${.CURDIR}/../../contrib/awk20040207
.PATH: ${AWKSRC}

PROG=	awk
SRCS=	awkgram.y b.c lex.c lib.c main.c parse.c proctab.c run.c tran.c ytab.h

CFLAGS+= -I. -I${AWKSRC} -DHAS_ISBLANK

DPADD=	${LIBM}
LDADD=	-lm

CLEANFILES= maketab proctab.c ytab.h maketab.nx

ytab.h: awkgram.h
	ln -sf ${.ALLSRC} ${.TARGET}

proctab.c: maketab.nx
	./maketab.nx > proctab.c

build-tools: maketab.nx

maketab.nx: ytab.h ${AWKSRC}/maketab.c

.include <bsd.prog.mk>
