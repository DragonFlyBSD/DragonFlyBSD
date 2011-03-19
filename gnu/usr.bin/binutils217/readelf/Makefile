.include "../Makefile.inc0"

.PATH: ${SRCDIR}/binutils ${SRCDIR}/binutils/doc

PROG=	readelf
WARNS=	1
CFLAGS+= -I${.CURDIR}/${RELTOP}/libbinutils -I${SRCDIR}/binutils
CFLAGS+= -I${SRCDIR}/bfd -I${.OBJDIR}/../libbfd

DPADD=	${RELTOP}/libbinutils/libbinutils.a
DPADD+=	${RELTOP}/libbfd/libbfd.a
DPADD+=	${RELTOP}/libiberty/libiberty.a
LDADD=	${DPADD}

.include <bsd.prog.mk>
