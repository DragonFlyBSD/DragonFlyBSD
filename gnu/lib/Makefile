# $FreeBSD: src/gnu/lib/Makefile,v 1.25.2.4 2001/01/06 23:16:53 obrien Exp $
# $DragonFly: src/gnu/lib/Makefile,v 1.5 2004/02/02 05:43:10 dillon Exp $

SUBDIR= libdialog libgmp libmp libregex libreadline

.if ${MACHINE_ARCH} == "i386"
SUBDIR+=gcc2 gcc3
.else
SUBDIR+=gcc3
.endif

.if !defined(NO_FORTRAN)
SUBDIR+= libg2c
.endif

.include <bsd.subdir.mk>
