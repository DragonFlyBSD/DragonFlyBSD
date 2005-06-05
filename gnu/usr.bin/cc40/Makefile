# $DragonFly: src/gnu/usr.bin/cc40/Attic/Makefile,v 1.1 2005/06/05 22:43:19 corecode Exp $

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
