# csu and libgcc* are normally built earlier by the _startup_libs50 target
# libitm is a c++ library, so it must be built after libstcxx
# libitm is not currently hooked into the build

#SUBDIR_ORDERED= libstdcxx libitm
SUBDIR_ORDERED=

SUBDIR+=	csu
SUBDIR+=	libgcc
SUBDIR+=	libgcc_eh
SUBDIR+=	libgcc_pic
SUBDIR+=	libgcov
SUBDIR+=	libgomp
#SUBDIR+=	libitm
SUBDIR+=	libobjc
SUBDIR+=	libssp
SUBDIR+=	libssp_nonshared
SUBDIR+=	libstdcxx

.include <bsd.subdir.mk>
