.include "../Makefile.bu"

.PATH: ${SRCDIR}/binutils ${SRCDIR}/binutils/doc

PROG=	objdump
SRCS=	objdump.c
WARNS=	1

DPADD+=	${.OBJDIR}/../libopcodes/libopcodes.a
LDADD+=	${.OBJDIR}/../libopcodes/libopcodes.a

.include <bsd.prog.mk>
