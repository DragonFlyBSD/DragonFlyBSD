.include "../Makefile.inc0"
.PATH: ${SRCDIR}/info ${SRCDIR}/doc

PROG=		infokey

CFLAGS+=	-I${SRCDIR} -I${SRCDIR}/info -I${SRCDIR}/gnulib/lib
CFLAGS+=	-I${.CURDIR}/../libgnu -I${.CURDIR}/../info
CFLAGS+=	-DHAVE_CONFIG_H

SRCS=		infokey.c\
		key.c

.include <bsd.prog.mk>
