# if wrapper mode
.if (defined(NO_CROSSTOOLS) && defined(BOOTSTRAPPING)) || \
    (!defined(BOOTSTRAPPING) && defined(NO_TOOLCHAIN))
.include "Makefile.zhack"
.else

# normal mode
SUBDIR= 	cc_prep
SUBDIR+=	libiberty
SUBDIR+=	libdecnumber
SUBDIR+=	cc_tools
.if !defined(REALLY_NOSHARED)
SUBDIR+=	liblto_plugin
.endif
SUBDIR+=	libbackend
SUBDIR+=	libcommon
SUBDIR+=	libcommon-target
SUBDIR+=	libcpp
SUBDIR+=	cc1
SUBDIR+=	cc
SUBDIR+=	cpp
SUBDIR+=	gcov
SUBDIR+=	cc1plus
SUBDIR+=	c++

.ORDER: ${SUBDIR}

.include <bsd.subdir.mk>
.endif
