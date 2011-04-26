.include "../Makefile.inc0"
.PATH: ${SRCDIR}/src ${SRCDIR}/doc

PROG=		grep
MAN=		grep.1
MLINKS+=	grep.1 fgrep.1
MLINKS+=	grep.1 egrep.1

CFLAGS+=	-I${.CURDIR}/../libgrep

.include <bsd.prog.mk>
