# csu and libgcc are normally built earlier by the _startup_libs44 target
# set SUBDIR_ORDERED to null to maximize parallelism

SUBDIR=		csu
SUBDIR+=	libgcc
SUBDIR+=	libgcov

.if !defined(NO_CXX)
SUBDIR+=	libsupc++
SUBDIR+=	libstdc++
.endif

.if !defined(NO_OBJC)
SUBDIR+=	libobjc
.endif

SUBDIR_ORDERED=

.include <bsd.subdir.mk>
