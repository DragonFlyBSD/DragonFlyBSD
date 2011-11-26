.include "../Makefile.bu"

.PATH: ${SRCDIR}/binutils ${SRCDIR}/binutils/doc

PROG=	addr2line
SRCS=	addr2line.c
MAN=	addr2line220.1
WARNS=	2

addr2line220.1: ${SRCDIR}/binutils/doc/addr2line.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	addr2line220.1

.include <bsd.prog.mk>
