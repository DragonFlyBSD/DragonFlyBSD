.include "../Makefile.inc1"
.include "../Makefile.bu"

.PATH: ${SRCDIR}/binutils ${SRCDIR}/binutils/doc

PROG=	objdump
SRCS=	elfcomm.c objdump.c
MFILE=	objdump${MANPAGEVER}.1
MAN=	${MFILE}
WARNS=	1

DPADD+=	${.OBJDIR}/../libopcodes/libopcodes.a
LDADD+=	${.OBJDIR}/../libopcodes/libopcodes.a

${MFILE}: ${SRCDIR}/binutils/doc/objdump.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	${MFILE}

.if defined(IS_PRIMARY)
MLINKS+=	${MFILE} objdump.1
.endif

.include <bsd.prog.mk>
