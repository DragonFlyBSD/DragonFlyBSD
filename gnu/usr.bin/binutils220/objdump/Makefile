.include "../Makefile.bu"

.PATH: ${SRCDIR}/binutils ${SRCDIR}/binutils/doc

PROG=	objdump
MAN=	objdump220.1
WARNS=	1

DPADD+=	${.OBJDIR}/../libopcodes/libopcodes.a
LDADD+=	${.OBJDIR}/../libopcodes/libopcodes.a

objdump220.1: ${SRCDIR}/binutils/doc/objdump.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	objcopy220.1

.include <bsd.prog.mk>
