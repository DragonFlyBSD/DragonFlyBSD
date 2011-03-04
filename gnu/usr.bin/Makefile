# $FreeBSD: src/gnu/usr.bin/Makefile,v 1.51 2000/01/16 00:11:34 obrien Exp $

SUBDIR= dialog diff diff3 grep groff man rcs sdiff sort texinfo

.if !defined(NO_CVS)
SUBDIR+=cvs
.endif

.if !defined(NO_GDB)
SUBDIR+=gdb
.endif

SUBDIR+= binutils217
SUBDIR+= binutils220

.if !defined(NO_GCC41)
SUBDIR+= cc41
.ORDER: binutils217 cc41
.endif

SUBDIR+= cc44
.ORDER: binutils217 cc44

.include <bsd.subdir.mk>
