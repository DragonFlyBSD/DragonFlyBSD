# Processed in order because SUBDIR_ORDERED is not defined
# csu and libgcc are normally built earlier by the _startup_libs44 target

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

.include <bsd.subdir.mk>
