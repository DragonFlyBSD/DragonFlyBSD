# csu and libgcc* are normally built earlier by the _startup_libs47 target
# libsupc++ must be built after libstdc++, everything else can be parallel

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
SUBDIR_ORDERED= libstdc++ libsupc++
.else
SUBDIR_ORDERED=
.endif

.if !defined(NO_OBJC)
SUBDIR+=	libobjc
.endif

.include <bsd.subdir.mk>
