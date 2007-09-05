# $FreeBSD: src/gnu/usr.bin/Makefile,v 1.51 2000/01/16 00:11:34 obrien Exp $
# $DragonFly: src/gnu/usr.bin/Makefile,v 1.32 2007/09/05 21:40:10 pavalos Exp $

SUBDIR= cpio dialog diff diff3 grep groff man rcs sdiff sort texinfo

.if !defined(NO_CVS)
SUBDIR+=cvs
.endif

.if !defined(NO_GDB)
SUBDIR+=gdb
.endif

SUBDIR+= binutils217

.if !defined(NO_GCC34)
SUBDIR+= cc34
.ORDER: binutils217 cc34
.endif

SUBDIR+= cc41
.ORDER: binutils217 cc41

.include <bsd.subdir.mk>
