# $FreeBSD: src/gnu/usr.bin/Makefile,v 1.51 2000/01/16 00:11:34 obrien Exp $
# $DragonFly: src/gnu/usr.bin/Makefile,v 1.31 2007/08/09 22:50:37 corecode Exp $

SUBDIR= cpio dialog diff diff3 grep groff man rcs sdiff sort texinfo

.if !defined(NO_CVS)
SUBDIR+=cvs
.endif

.if !defined(NO_GDB)
SUBDIR+=gdb
.endif

SUBDIR+= binutils217
SUBDIR+= cc34
.ORDER: binutils217 cc34

SUBDIR+= cc41
.ORDER: binutils217 cc41

.include <bsd.subdir.mk>
