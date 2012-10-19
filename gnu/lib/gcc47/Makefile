SUBDIR+=	csu
SUBDIR+=	libgcc
SUBDIR+=	libgcc_eh
SUBDIR+=	libgcc_pic
SUBDIR+=	libgcov
SUBDIR+=	libssp
SUBDIR+=	libssp_nonshared

.if !defined(NO_CXX)
SUBDIR+=	libstdc++
SUBDIR+=	libsupc++
.endif

.if !defined(NO_OBJC)
SUBDIR+=	libobjc
.endif

.include <bsd.subdir.mk>
