# $DragonFly: src/gnu/usr.bin/gdb/libiberty/Makefile,v 1.1 2004/10/23 17:02:38 joerg Exp $

.include "../Makefile.inc"

LIB=	iberty
INTERNALLIB=	true

CONTRIBDIR=	${BASEDIR}/libiberty
.PATH:	${CONTRIBDIR}

CFLAGS+=	-I${BASEDIR}/include -I${.CURDIR} -DHAVE_CONFIG_H

SRCS=	cp-demangle.c cplus-dem.c floatformat.c getopt.c
SRCS+=	getopt1.c getruntime.c hashtab.c objalloc.c obstack.c
SRCS+=	regex.c safe-ctype.c splay-tree.c

SRCS+=	argv.c concat.c hex.c lbasename.c xexit.c xstrdup.c xstrerror.c

.include <bsd.lib.mk>
