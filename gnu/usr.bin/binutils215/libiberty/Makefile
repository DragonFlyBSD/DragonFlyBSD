# $DragonFly: src/gnu/usr.bin/binutils215/libiberty/Attic/Makefile,v 1.1 2004/12/20 13:14:45 asmodai Exp $

.include "../Makefile.inc0"

.PATH: ${SRCDIR}/libiberty

LIB=	iberty
SRCS=	argv.c choose-temp.c concat.c cp-demangle.c cp-demint.c cplus-dem.c \
	dyn-string.c getopt.c getopt1.c getpwd.c getruntime.c \
	hex.c floatformat.c hashtab.c lbasename.c lrealpath.c \
	make-temp-file.c make-relative-prefix.c \
	objalloc.c obstack.c safe-ctype.c xatexit.c xexit.c xmalloc.c \
	xstrdup.c xstrerror.c
.if defined(BOOTSTRAPPING)
SRCS+=	basename.c
.endif
WARNS=	1
CFLAGS+=	-DHAVE_CONFIG_H
INTERNALLIB=	true

.include <bsd.lib.mk>
