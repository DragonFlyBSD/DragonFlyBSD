.include "Makefile.inc"

SUBDIR=		csu libgcc libgcov

.if !defined(NO_CXX)
SUBDIR+=	libsupc++ libstdc++
.endif

.if !defined(NO_OBJC)
SUBDIR+=	libobjc
.endif

.include <bsd.subdir.mk>
