LIB=		iberty
INTERNALLIB=	YES

SRCS=	concat.c cp-demangle.c cp-demint.c cplus-dem.c dyn-string.c \
	fibheap.c fopen_unlocked.c getpwd.c getruntime.c hashtab.c hex.c \
	lbasename.c lrealpath.c make-relative-prefix.c make-temp-file.c \
	md5.c obstack.c partition.c pex-unix.c physmem.c safe-ctype.c \
	splay-tree.c xexit.c xmalloc.c xmemdup.c xstrdup.c xstrerror.c \
	filename_cmp.c regex.c xstrndup.c

GCC_NO_PATH=	# defined

# copy cc47/libiberty/config.h, put it here
CFLAGS+= -I${.CURDIR}

.include <bsd.hostlib.mk>

.PATH:	${GCCDIR}/libiberty
