# $FreeBSD: src/gnu/usr.bin/Makefile,v 1.51 2000/01/16 00:11:34 obrien Exp $
# $DragonFly: src/gnu/usr.bin/Makefile,v 1.25 2007/01/17 12:09:42 swildner Exp $

SUBDIR= cpio dialog diff diff3 grep groff man rcs sdiff sort tar texinfo

.if !defined(NO_CVS)
SUBDIR+=cvs
.endif

.if !defined(NO_GDB)
SUBDIR+=gdb
.endif

SUBDIR+= binutils215 cc34
.ORDER: binutils215 cc34

.if defined(WANT_GCC41)
SUBDIR+= binutils215 cc41
.ORDER: binutils215 cc41
.endif

.include <bsd.subdir.mk>
