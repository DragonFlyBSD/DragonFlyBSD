SUBDIR= 	cc_prep
SUBDIR+=	libiberty
SUBDIR+=	libdecnumber
SUBDIR+=	cc_tools
SUBDIR+=	liblto_plugin
SUBDIR+=	libbackend
SUBDIR+=	libcommon
SUBDIR+=	libcommon-target
SUBDIR+=	libcpp
SUBDIR+=	cc1
SUBDIR+=	cc
SUBDIR+=	doc
SUBDIR+=	cpp
SUBDIR+=	gcov
.if !defined(NO_CXX)
SUBDIR+=	cc1plus
SUBDIR+=	c++
.endif
.if !defined(NO_OBJC)
SUBDIR+=	cc1obj
.endif

.ORDER: ${SUBDIR}

.include <bsd.subdir.mk>
