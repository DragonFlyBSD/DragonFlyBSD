.include "../../Makefile.inc1"
.include "../Makefile.gld"

PROG_CXX=	ld.gold
SRCS=		main.cc ${GOLD_COMMON}
CFLAGS+=	-I${.CURDIR}/../../block2/libgold
LDADD=		${STD_LDADD}
NOMAN=

# link-time optimization requires dlopen() which means ld cannot be built
# statically and also support -plugin, not even for cross-tools.
.if !defined(REALLY_NOSHARED)
.undef NOSHARED
.endif

WARNS=	1

.include <bsd.prog.mk>
