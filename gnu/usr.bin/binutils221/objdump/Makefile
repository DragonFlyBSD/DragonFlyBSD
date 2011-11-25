.include "../Makefile.bu"

.PATH: ${SRCDIR}/binutils ${SRCDIR}/binutils/doc

PROG=	objdump
SRCS=	elfcomm.c objdump.c
MAN=	objdump221.1
WARNS=	1

DPADD+=	${.OBJDIR}/../libopcodes/libopcodes.a
LDADD+=	${.OBJDIR}/../libopcodes/libopcodes.a

objdump221.1: ${SRCDIR}/binutils/doc/objdump.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	objdump221.1

.include <bsd.prog.mk>
