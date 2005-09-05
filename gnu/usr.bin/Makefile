# $FreeBSD: src/gnu/usr.bin/Makefile,v 1.51 2000/01/16 00:11:34 obrien Exp $
# $DragonFly: src/gnu/usr.bin/Makefile,v 1.22 2005/09/05 15:36:34 corecode Exp $
#
# Note that gcc2 is only built under the i386 architecture.  Other
# architectures require gcc3.

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
