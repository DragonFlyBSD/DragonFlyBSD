.include "../Makefile.bu"

.PATH: ${SRCDIR}/binutils ${SRCDIR}/binutils/doc

PROG=	objdump
SRCS=	elfcomm.c objdump.c
MAN=	objdump222.1
MLINKS+=objdump222.1 objdump.1
WARNS=	1

CFLAGS+= -DOBJDUMP_PRIVATE_VECTORS=""

DPADD+=	${.OBJDIR}/../libopcodes/libopcodes.a
LDADD+=	${.OBJDIR}/../libopcodes/libopcodes.a

objdump222.1: ${SRCDIR}/binutils/doc/objdump.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	objdump222.1

.include <bsd.prog.mk>
