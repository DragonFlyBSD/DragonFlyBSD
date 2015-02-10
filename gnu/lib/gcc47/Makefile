# csu and libgcc* are normally built earlier by the _startup_libs47 target
# libsupc++ must be built after libstdc++, everything else can be parallel

SUBDIR_ORDERED= libstdc++ libsupc++

SUBDIR+=	csu
SUBDIR+=	libgcc
SUBDIR+=	libgcc_eh
SUBDIR+=	libgcc_pic
SUBDIR+=	libgcov
SUBDIR+=	libssp
SUBDIR+=	libssp_nonshared
SUBDIR+=	libgomp
SUBDIR+=	libstdc++
SUBDIR+=	libsupc++
SUBDIR+=	libobjc

.include <bsd.subdir.mk>
