# $FreeBSD: src/gnu/usr.bin/Makefile,v 1.51 2000/01/16 00:11:34 obrien Exp $
# $DragonFly: src/gnu/usr.bin/Makefile,v 1.23 2006/09/09 00:30:48 pavalos Exp $

SUBDIR= cpio dialog diff diff3 grep groff man rcs sdiff sort tar texinfo

.if !defined(NO_CVS)
SUBDIR+=cvs
.endif

.if !defined(NO_GDB)
SUBDIR+=gdb
.endif

SUBDIR+= binutils215 cc34
.ORDER: binutils215 cc34

.if defined(WANT_GCC40)
SUBDIR+= binutils215 cc40
.ORDER: binutils215 cc40
.endif

.include <bsd.subdir.mk>
