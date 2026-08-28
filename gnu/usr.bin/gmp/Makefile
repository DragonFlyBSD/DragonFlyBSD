# if wrapper mode
.if (defined(NO_CROSSTOOLS) && defined(BOOTSTRAPPING)) || \
    (!defined(BOOTSTRAPPING) && defined(NO_TOOLCHAIN))
.include "Makefile.zhack"
.else

# normal mode
SUBDIR=	tools lib

.include <bsd.subdir.mk>
.endif
