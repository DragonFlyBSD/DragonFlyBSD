# $FreeBSD: src/gnu/lib/Makefile,v 1.25.2.4 2001/01/06 23:16:53 obrien Exp $
# $DragonFly: src/gnu/lib/Makefile,v 1.2 2003/06/17 04:25:43 dillon Exp $

SUBDIR= csu libdialog libgcc libgmp libmp libregex libreadline

.if ${OBJFORMAT} == aout
.if !defined(NOLIBC_R)
SUBDIR+= libgcc_r
.endif
.endif

.if !defined(NO_CXX)
SUBDIR+= libstdc++
.endif

.if !defined(NO_OBJC)
SUBDIR+= libobjc
.endif

.if !defined(NO_FORTRAN)
SUBDIR+= libg2c
.endif

.include <bsd.subdir.mk>
