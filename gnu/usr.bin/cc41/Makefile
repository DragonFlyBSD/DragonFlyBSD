# $DragonFly: src/gnu/usr.bin/cc41/Makefile,v 1.1 2006/09/27 12:10:33 corecode Exp $

SUBDIR=		cc_prep
SUBDIR+=	libiberty
SUBDIR+=	cc_tools libbackend libcpp cc1 cc doc
SUBDIR+=	cpp
SUBDIR+=	gcov
.if !defined(NO_CXX)
SUBDIR+=	cc1plus c++ c++filt
.endif
#.if !defined(NO_FORTRAN)
#SUBDIR+=	f771 g77
#.endif
#.if !defined(NO_OBJC)
#SUBDIR+=	cc1obj
#.endif

.ORDER: ${SUBDIR}

.include <bsd.subdir.mk>
