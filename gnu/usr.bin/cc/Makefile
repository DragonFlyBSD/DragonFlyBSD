# $FreeBSD: src/gnu/usr.bin/cc/Makefile,v 1.21.2.3 2002/11/15 18:18:38 ru Exp $
# $DragonFly: src/gnu/usr.bin/cc/Attic/Makefile,v 1.3 2004/03/22 20:57:44 dillon Exp $

# The order of some of these are rather important.  Some depend on previous
# subdirs.

SUBDIR= cc_tools cc_int cccp cc1 cc

.if !defined(NO_CPP)
SUBDIR+= cpp
.endif

.if !defined(NO_CXX)
SUBDIR+= cc1plus c++ c++filt doc
.endif

.if !defined(NO_OBJC)
SUBDIR+= cc1obj
.endif

.if !defined(NO_FORTRAN)
SUBDIR+= f77 f771 f77doc
.endif

.if !defined(NO_GCOV)
SUBDIR+= gcov
.endif

# interdependant header files
.ORDER: ${SUBDIR}

.include <bsd.subdir.mk>
