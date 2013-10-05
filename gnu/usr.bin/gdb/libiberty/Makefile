.include "../Makefile.inc"

LIB=	iberty
INTERNALLIB=	true

CONTRIBDIR=	${BASEDIR}/libiberty
.PATH:	${CONTRIBDIR}

CFLAGS+=	-I${BASEDIR}/include -I${.CURDIR} -DHAVE_CONFIG_H

SRCS+=	./argv.c \
	./cp-demangle.c \
	./cp-demint.c \
	./cplus-dem.c \
	./concat.c \
	./dwarfnames.c \
	./dyn-string.c \
	./filename_cmp.c \
	./floatformat.c \
	./getopt.c \
	./getopt1.c \
	./getpwd.c \
	./getruntime.c \
	./hashtab.c \
	./hex.c \
	./lbasename.c \
	./lrealpath.c \
	./make-relative-prefix.c \
	./make-temp-file.c \
	./objalloc.c \
	./obstack.c \
	./pex-common.c \
	./pex-one.c \
	./pex-unix.c \
	./pexecute.c \
	./regex.c \
	./safe-ctype.c \
	./splay-tree.c \
	./timeval-utils.c \
	./unlink-if-ordinary.c \
	./xexit.c \
	./xstrdup.c \
	./xstrerror.c

.include <bsd.lib.mk>
