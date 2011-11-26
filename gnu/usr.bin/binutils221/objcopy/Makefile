.include "../Makefile.bu"

.PATH: ${SRCDIR}/binutils ${SRCDIR}/binutils/doc

PROG=	objcopy
SRCS=	not-strip.c objcopy.c
MAN=	objcopy221.1
MLINKS+=objcopy221.1 objcopy.1
WARNS=	1

objcopy221.1: ${SRCDIR}/binutils/doc/objcopy.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	objcopy221.1

.include <bsd.prog.mk>
