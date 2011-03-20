.include "../Makefile.inc0"

.PATH: ${SRCDIR}/binutils ${SRCDIR}/binutils/doc

PROG=	objcopy
SRCS=	debug.c ieee.c not-strip.c objcopy.c rename.c rdcoff.c rddbg.c \
	stabs.c wrstabs.c
MAN=	objcopy217.1
MLINKS+=objcopy217.1 objcopy.1
WARNS=	1
CFLAGS+= -I${.CURDIR}/${RELTOP}/libbinutils
CFLAGS+= -I${SRCDIR}/binutils
CFLAGS+= -I${SRCDIR}/bfd -I${.OBJDIR}/../libbfd
DPADD=	${RELTOP}/libbinutils/libbinutils.a
DPADD+=	${RELTOP}/libbfd/libbfd.a
DPADD+=	${RELTOP}/libiberty/libiberty.a
LDADD=	${DPADD}

objcopy217.1: ${SRCDIR}/binutils/doc/objcopy.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	objcopy217.1

.include <bsd.prog.mk>
