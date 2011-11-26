.include "../Makefile.inc0"

.PATH: ${SRCDIR}/libiberty

LIB=	iberty
SRCS=	argv.c choose-temp.c concat.c cp-demangle.c cp-demint.c cplus-dem.c \
	dyn-string.c filename_cmp.c getopt.c getopt1.c getpwd.c getruntime.c \
	hex.c floatformat.c hashtab.c lbasename.c lrealpath.c \
	make-temp-file.c make-relative-prefix.c md5.c \
	objalloc.c obstack.c safe-ctype.c sha1.c stpcpy.c unlink-if-ordinary.c \
	xatexit.c xexit.c xmalloc.c \
	xstrdup.c xstrerror.c
WARNS=	1
CFLAGS+=	-DHAVE_CONFIG_H
INTERNALLIB=	true

.include <bsd.lib.mk>
