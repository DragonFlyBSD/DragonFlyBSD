# $DragonFly: src/gnu/usr.bin/cc34/Makefile,v 1.3 2004/12/21 13:10:47 joerg Exp $

SUBDIR=		cc_prep cc_tools libbackend libcpp cc1 cc doc
SUBDIR+=	cpp
SUBDIR+=	gcov
.if !defined(NO_CXX)
SUBDIR+=	cc1plus c++ c++filt
.endif
.if !defined(NO_FORTRAN)
SUBDIR+=	f771 g77
.endif
.if !defined(NO_OBJC)
SUBDIR+=	cc1obj
.endif

.ORDER: ${SUBDIR}

.include <bsd.subdir.mk>
