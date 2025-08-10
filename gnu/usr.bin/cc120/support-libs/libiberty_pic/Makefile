GCC_NO_PATH=	yes
GCC_NO_LIBS=	yes
LOCAL_CONFIG=	yes
.include "../Makefile.inc"
.PATH:	${GCCDIR}/libiberty

LIB=		iberty_pic
NOPROFILE=	yes
INTERNALLIB=	yes

# Bare minimum for liblto_plugin.so, this is a special case
NOSHARED=	YES
CFLAGS+=	-fpic

GOOD_CONFIG=	${.CURDIR}/../libiberty/config.h
CLEANFILES=	config.h

SRCS=		argv.c \
		concat.c \
		hashtab.c \
		make-temp-file.c \
		pex-common.c \
		pex-unix.c \
		safe-ctype.c \
		simple-object-coff.c \
		simple-object-elf.c \
		simple-object-mach-o.c \
		simple-object-xcoff.c \
		simple-object.c \
		unlink-if-ordinary.c \
		vprintf-support.c \
		xasprintf.c \
		xexit.c \
		xmalloc.c \
		xstrdup.c \
		xstrerror.c \
		xvasprintf.c

config.h: ${GOOD_CONFIG}
	cp ${.ALLSRC} .

depend all: config.h

.include <bsd.lib.mk>
