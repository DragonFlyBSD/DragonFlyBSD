# $FreeBSD: src/gnu/usr.bin/Makefile,v 1.51 2000/01/16 00:11:34 obrien Exp $

SUBDIR= dialog diff grep groff man rcs texinfo

.if !defined(NO_CVS)
SUBDIR+=cvs
.endif

.if !defined(NO_GDB)
SUBDIR+=gdb
.endif

SUBDIR+= binutils222
SUBDIR+= binutils221

.if !defined(NO_GCC41)
SUBDIR+= cc41
.ORDER: binutils222 cc41
.endif

SUBDIR+= cc44
.ORDER: binutils222 cc44

.include <bsd.subdir.mk>
