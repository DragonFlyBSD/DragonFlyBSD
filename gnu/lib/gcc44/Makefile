.include "Makefile.inc"

SUBDIR=		csu libgcc libgcov

.if !defined(NO_CXX)
SUBDIR+=	libsupc++ libstdc++
.endif

SUBDIR+=	libobjc

.include <bsd.subdir.mk>
