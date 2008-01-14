# $DragonFly: src/gnu/usr.bin/gdb/libiberty/Makefile,v 1.2 2008/01/14 21:36:39 corecode Exp $

.include "../Makefile.inc"

LIB=	iberty
INTERNALLIB=	true

CONTRIBDIR=	${BASEDIR}/libiberty
.PATH:	${CONTRIBDIR}

CFLAGS+=	-I${BASEDIR}/include -I${.CURDIR} -DHAVE_CONFIG_H

SRCS=	cp-demangle.c cplus-dem.c cp-demint.c floatformat.c getopt.c
SRCS+=	getopt1.c getruntime.c hashtab.c objalloc.c obstack.c
SRCS+=	regex.c safe-ctype.c splay-tree.c

SRCS+=	argv.c concat.c hex.c lbasename.c xexit.c xstrdup.c xstrerror.c
SRCS+=	lrealpath.c make-relative-prefix.c filename_cmp.c stpcpy.c unlink-if-ordinary.c

.include <bsd.lib.mk>
