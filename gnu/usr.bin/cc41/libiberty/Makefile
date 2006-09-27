# $DragonFly: src/gnu/usr.bin/cc41/libiberty/Makefile,v 1.1 2006/09/27 12:10:35 corecode Exp $

LIB=		iberty
INTERNALLIB=	YES

SRCS=	concat.c cp-demangle.c cp-demint.c cplus-dem.c dyn-string.c \
	fibheap.c fopen_unlocked.c getpwd.c getruntime.c hashtab.c hex.c lbasename.c \
	lrealpath.c make-relative-prefix.c make-temp-file.c md5.c obstack.c \
	partition.c pex-common.c pex-one.c pex-unix.c physmem.c safe-ctype.c splay-tree.c xexit.c \
	xmalloc.c xmemdup.c xstrdup.c xstrerror.c regex.c strverscmp.c

GCC_NO_PATH=	# defined

CFLAGS+= -I${.CURDIR}

.include <bsd.lib.mk>

.PATH:	${GCCDIR}/libiberty
