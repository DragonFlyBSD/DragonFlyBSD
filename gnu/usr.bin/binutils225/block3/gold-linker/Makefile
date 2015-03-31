.include "../../Makefile.inc1"
.include "../Makefile.gld"

PROG_CXX=	ld.gold
SRCS=		main.cc
CFLAGS+=	-I${.CURDIR}/../../block2/libgold
LDADD=		${STD_LDADD}
NOMAN=

# link-time optimization requires dlopen() which means ld cannot be built
# statically and also support -plugin, not even for cross-tools.
.undef NOSHARED

.include <bsd.prog.mk>
