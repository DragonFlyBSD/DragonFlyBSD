# Processed in order because SUBDIR_ORDERED is not defined
# csu and libgcc* are normally built earlier by the _startup_libs47 target

SUBDIR+=	csu
SUBDIR+=	libgcc
SUBDIR+=	libgcc_eh
SUBDIR+=	libgcc_pic
SUBDIR+=	libgcov
SUBDIR+=	libssp
SUBDIR+=	libssp_nonshared
SUBDIR+=	libgomp

.if !defined(NO_CXX)
SUBDIR+=	libstdc++
SUBDIR+=	libsupc++
.endif

.if !defined(NO_OBJC)
SUBDIR+=	libobjc
.endif

.include <bsd.subdir.mk>
