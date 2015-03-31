.include "../../Makefile.inc1"
.include "../Makefile.gld"

PROG_CXX=	incremental-dump
SRCS=		incremental-dump.cc
CFLAGS+=	-I${.CURDIR}/../../block2/libgold
LDADD=		${STD_LDADD}
NOMAN=

.include <bsd.prog.mk>
