# $DragonFly: src/gnu/usr.bin/cc34/Makefile,v 1.4 2008/05/14 15:02:47 hasso Exp $

SUBDIR=		cc_prep cc_tools libbackend libcpp cc1 cc doc
SUBDIR+=	cpp
SUBDIR+=	gcov
.if !defined(NO_CXX)
SUBDIR+=	cc1plus c++ c++filt
.endif
.if !defined(NO_OBJC)
SUBDIR+=	cc1obj
.endif

.ORDER: ${SUBDIR}

.include <bsd.subdir.mk>
