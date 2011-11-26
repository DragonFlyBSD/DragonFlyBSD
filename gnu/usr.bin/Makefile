# $FreeBSD: src/gnu/usr.bin/Makefile,v 1.51 2000/01/16 00:11:34 obrien Exp $

SUBDIR= dialog diff grep groff man rcs texinfo

.if !defined(NO_CVS)
SUBDIR+=cvs
.endif

.if !defined(NO_GDB)
SUBDIR+=gdb
.endif

# XXX marino: Step 1 for moving back to a single binutils system
#SUBDIR+= binutils220
SUBDIR+= binutils221

.if !defined(NO_GCC41)
SUBDIR+= cc41
.ORDER: binutils221 cc41
.endif

SUBDIR+= cc44
.ORDER: binutils221 cc44

.include <bsd.subdir.mk>
