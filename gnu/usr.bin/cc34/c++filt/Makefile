# $DragonFly: src/gnu/usr.bin/cc34/c++filt/Makefile,v 1.2 2004/06/20 20:59:21 joerg Exp $

.include "../Makefile.inc"

#.PATH: ${GCCDIR}/gcc ${GCCDIR}/libiberty

PROG=	c++filt
SRCS=	cp-demangle.c cplus-dem.c
SRCS+=  dyn-string.c safe-ctype.c xexit.c xmalloc.c xstrdup.c
NOMAN=	1

CFLAGS+= -DSTANDALONE_DEMANGLER -DIN_GCC -DVERSION=\"$(version)\"

.if defined(BOOTSTRAPPING)
SRCS+=	getopt_long.c
.endif

DPADD+=	${LIBCC_INT}
LDADD+=	${LIBCC_INT}

.include <bsd.prog.mk>
