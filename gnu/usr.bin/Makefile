# $FreeBSD: src/gnu/usr.bin/Makefile,v 1.51 2000/01/16 00:11:34 obrien Exp $
# $DragonFly: src/gnu/usr.bin/Makefile,v 1.29 2007/07/23 23:03:34 dillon Exp $

SUBDIR= cpio dialog diff diff3 grep groff man rcs sdiff sort texinfo

.if !defined(NO_CVS)
SUBDIR+=cvs
.endif

.if !defined(NO_GDB)
SUBDIR+=gdb
.endif

SUBDIR+= binutils215 binutils217
SUBDIR+= cc34
.ORDER: binutils215 binutils217 cc34

.if !defined(NO_GCC41)
SUBDIR+= cc41
.ORDER: binutils215 binutils217 cc41
.endif

.include <bsd.subdir.mk>
