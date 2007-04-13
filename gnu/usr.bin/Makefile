# $FreeBSD: src/gnu/usr.bin/Makefile,v 1.51 2000/01/16 00:11:34 obrien Exp $
# $DragonFly: src/gnu/usr.bin/Makefile,v 1.28 2007/04/13 12:26:42 corecode Exp $

SUBDIR= cpio dialog diff diff3 grep groff man rcs sdiff sort texinfo

.if !defined(NO_CVS)
SUBDIR+=cvs
.endif

.if !defined(NO_GDB)
SUBDIR+=gdb
.endif

SUBDIR+= binutils215 binutils217 cc34
.ORDER: binutils215 binutils217 cc34

.if !defined(NO_GCC41)
SUBDIR+= binutils215 binutils217 cc41
.ORDER: binutils215 binutils217 cc41
.endif

.include <bsd.subdir.mk>
