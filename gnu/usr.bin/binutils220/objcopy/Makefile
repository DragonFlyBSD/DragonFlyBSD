.include "../Makefile.bu"

.PATH: ${SRCDIR}/binutils ${SRCDIR}/binutils/doc

PROG=	objcopy
SRCS=	not-strip.c objcopy.c
MAN=	objcopy220.1
WARNS=	1

objcopy220.1: ${SRCDIR}/binutils/doc/objcopy.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	objcopy220.1

.include <bsd.prog.mk>
