.include "../Makefile.inc0"

.PATH: ${SRCDIR}/binutils ${SRCDIR}/binutils/doc

PROG=	addr2line
SRCS=	addr2line.c
MAN=	addr2line217.1
MLINKS+=addr2line217.1 addr2line.1
WARNS=	2
CFLAGS+= -I${.CURDIR}/${RELTOP}/libbinutils
CFLAGS+= -I${SRCDIR}/binutils -I${.OBJDIR}/../libbfd
DPADD=	${RELTOP}/libbinutils/libbinutils.a
DPADD+=	${RELTOP}/libbfd/libbfd.a
DPADD+=	${RELTOP}/libiberty/libiberty.a
LDADD=	${DPADD}

addr2line217.1: ${SRCDIR}/binutils/doc/addr2line.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	addr2line217.1

.include <bsd.prog.mk>
