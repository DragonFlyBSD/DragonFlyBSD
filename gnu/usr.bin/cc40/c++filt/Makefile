# $DragonFly: src/gnu/usr.bin/cc40/c++filt/Attic/Makefile,v 1.1 2005/06/05 22:43:20 corecode Exp $

.include "../Makefile.inc"

PROG=	c++filt
NOMAN=	1

SRCS=	cp-demangle.c cp-demint.c cplus-dem.c 
SRCS+=  dyn-string.c safe-ctype.c xexit.c xmalloc.c xstrdup.c

CFLAGS+= -DSTANDALONE_DEMANGLER -DIN_GCC -DVERSION=\"$(version)\"

DPADD+=	${LIBCC_INT}
LDADD+=	${LIBCC_INT}

GCC_NO_LIBERTY=	# defined
.PATH:	${GCCDIR}/libiberty

.include <bsd.prog.mk>
