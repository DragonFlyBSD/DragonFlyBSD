# $FreeBSD: src/gnu/lib/Makefile,v 1.25.2.4 2001/01/06 23:16:53 obrien Exp $
# $DragonFly: src/gnu/lib/Makefile,v 1.8 2005/01/11 14:36:02 joerg Exp $

SUBDIR= libdialog libregex libreadline gcc34

.if ${MACHINE_ARCH} == "i386"
SUBDIR+=gcc2
.endif

.if !defined(NO_FORTRAN)
SUBDIR+= libg2c
.endif

.include <bsd.subdir.mk>
