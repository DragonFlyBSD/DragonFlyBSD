GCC_NO_PATH=	yes
GCC_NO_LIBS=	yes
LOCAL_CONFIG=	yes
.include "../Makefile.inc"
.PATH:	${GCCDIR}/libiberty

LIB=		iberty
NOPROFILE=	yes
INTERNALLIB=	yes
GOOD_CONFIG=	${.CURDIR}/../libiberty/config.h
CLEANFILES=	config.h

INSTALL_PIC_ARCHIVE=	yes

SRCS=		hashtab.c \
		xmalloc.c \
		simple-object.c \
		simple-object-coff.c \
		simple-object-elf.c \
		simple-object-mach-o.c \
		simple-object-xcoff.c \
		xstrerror.c \
		pex-unix.c \
		pex-common.c \
		argv.c \
		safe-ctype.c \
		xexit.c \
		xstrdup.c \
		make-temp-file.c \
		concat.c

config.h: ${GOOD_CONFIG}
	cp ${.ALLSRC} .

depend all: config.h

.include <bsd.lib.mk>
