# $FreeBSD: src/gnu/usr.bin/cc/f771/Makefile,v 1.10 2004/01/11 20:45:46 ru Exp $
# $DragonFly: src/gnu/usr.bin/cc3/f771/Attic/Makefile,v 1.10 2004/06/21 03:25:33 dillon Exp $

.include "${.CURDIR}/../Makefile.inc"

CFLAGS+=        -I${.CURDIR}/../config

.PATH: ${GCCDIR}/gcc/f ${GCCDIR}/gcc ${GCCDIR}/libiberty

PROG=	f771
SRCS=	bad.c bit.c bld.c data.c equiv.c expr.c global.c implic.c info.c \
	intrin.c lab.c lex.c malloc.c name.c parse.c        src.c st.c sta.c \
	stb.c stc.c std.c ste.c storag.c stp.c str.c sts.c stt.c stu.c stv.c \
	stw.c symbol.c target.c top.c type.c where.c main.c \
	xmalloc.c xexit.c com.o com.h
NOMAN=
CFLAGS+= -D__FREEBSD__
CFLAGS+= -I${GCCDIR}/gcc/f -I.
CFLAGS+= -DMALLOC_RENAME

DPADD=	${LIBCC_INT} 
LDADD=	${LIBCC_INT}

# This is a mess.  PROG generates an objects-depend-on-all-headers 
# dependancy prior to building the depends, so we have to specify
# files not in SRCS
#
build-tools: fini
fini: fini.no xmalloc.no xexit.no
	${NXCC} ${NXCFLAGS} ${NXLDFLAGS} -o ${.TARGET} ${.ALLSRC}

CLEANFILES=	fini fini.no xmalloc.no xexit.no

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

# com.h needs to be patched for gcc3.4 to be able to compile it due to
# an erronous prototype.  This is really nasty because there are so many
# duplicate header file names we can't just reorder the -I list for all
# the sources, we must only do it for com.c
#
com.h:	${GCCDIR}/gcc/f/com.h
	cp ${.ALLSRC} ${.TARGET}.new
	patch ${.TARGET}.new ${.CURDIR}/com.h.patch
	mv -f ${.TARGET}.new ${.TARGET}

com.o:	${GCCDIR}/gcc/f/com.c com.h
	cp ${.ALLSRC:M*.c} .
	${CC} -I. -I${GCCDIR}/gcc/f ${CFLAGS} -c com.c

CLEANFILES+=	com.h com.h.new.orig com.c

.include <bsd.prog.mk>
