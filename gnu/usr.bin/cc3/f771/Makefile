# $FreeBSD: src/gnu/usr.bin/cc/f771/Makefile,v 1.10 2004/01/11 20:45:46 ru Exp $
# $DragonFly: src/gnu/usr.bin/cc3/f771/Attic/Makefile,v 1.5 2004/02/03 03:47:12 dillon Exp $

.include "${.CURDIR}/../Makefile.inc"

CFLAGS+=        -I${.CURDIR}/../config

.PATH: ${GCCDIR}/gcc/f ${GCCDIR}/gcc ${GCCDIR}/libiberty

PROG=	f771
SRCS=	bad.c bit.c bld.c com.c data.c equiv.c expr.c global.c implic.c info.c \
	intrin.c lab.c lex.c malloc.c name.c parse.c        src.c st.c sta.c \
	stb.c stc.c std.c ste.c storag.c stp.c str.c sts.c stt.c stu.c stv.c \
	stw.c symbol.c target.c top.c type.c where.c main.c \
	xmalloc.c xexit.c
NOMAN=

CFLAGS+= -I${GCCDIR}/gcc/f -I.

DPADD=	${LIBCC_INT} 
LDADD=	${LIBCC_INT}

build-tools: fini
fini: fini.o xmalloc.o xexit.o
	${CC} -static ${CFLAGS} ${LDFLAGS} -o ${.TARGET} ${.ALLSRC}

CLEANFILES=	fini fini.o

#-----------------------------------------------------------------------
# str-* gunk

.for i in 1t 2t fo io nq op ot
.ORDER: str-$i.h str-$i.j
str-$i.j str-$i.h: str-$i.fin fini
	./fini ${GCCDIR}/gcc/f/str-$i.fin str-$i.j str-$i.h

FINIHDRS+=	str-$i.j str-$i.h
.endfor

SRCS+=		${FINIHDRS:M*.h}
CLEANFILES+=	${FINIHDRS}

.include <bsd.prog.mk>
