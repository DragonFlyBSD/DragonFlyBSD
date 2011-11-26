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
SRCS+=	lrealpath.c make-relative-prefix.c filename_cmp.c stpcpy.c
SRCS+=	unlink-if-ordinary.c
SRCS+=	pexecute.c pex-common.c pex-one.c pex-unix.c make-temp-file.c

.include <bsd.lib.mk>
