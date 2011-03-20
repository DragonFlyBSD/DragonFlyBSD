.include "../Makefile.inc0"

.PATH: ${SRCDIR}/binutils ${SRCDIR}/binutils/doc

PROG=	ranlib
SRCS=	ar.c is-ranlib.c
MAN=	ranlib217.1
MLINKS+=ranlib217.1 ranlib.1
CFLAGS+= -I${.CURDIR}/${RELTOP}/libbinutils
CFLAGS+= -I${SRCDIR}/binutils
CFLAGS+= -I${SRCDIR}/bfd -I${.OBJDIR}/../libbfd

NOSHARED?= yes
DPADD=	${RELTOP}/libbinutils/libbinutils.a
DPADD+=	${RELTOP}/libbfd/libbfd.a
DPADD+=	${RELTOP}/libiberty/libiberty.a
LDADD=	${DPADD}

ranlib217.1: ${SRCDIR}/binutils/doc/ranlib.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	ranlib217.1

.include <bsd.prog.mk>
