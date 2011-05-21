SUBDIR=		cc_prep
SUBDIR+=	gmp mpfr
SUBDIR+=	libiberty libdecnumber
SUBDIR+=	cc_tools libbackend libcpp cc1 cc doc
SUBDIR+=	cpp
SUBDIR+=	gcov
.if !defined(NO_CXX)
SUBDIR+=	cc1plus c++
.endif
.if !defined(NO_OBJC)
SUBDIR+=	cc1obj
.endif

.ORDER: ${SUBDIR}

.include <bsd.subdir.mk>
