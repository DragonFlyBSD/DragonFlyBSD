# $FreeBSD: src/gnu/usr.bin/Makefile,v 1.51 2000/01/16 00:11:34 obrien Exp $
# $DragonFly: src/gnu/usr.bin/Makefile,v 1.18 2005/01/04 19:14:18 joerg Exp $
#
# Note that gcc2 is only built under the i386 architecture.  Other
# architectures require gcc3.

SUBDIR= cpio dialog diff diff3 gperf grep groff man rcs sdiff \
        sort tar texinfo

.if !defined(NO_CVS)
SUBDIR+=cvs
.endif

.if !defined(NO_GDB)
SUBDIR+=gdb
.endif

SUBDIR+= binutils215 cc34
.ORDER: binutils215 cc34

.if ${MACHINE_ARCH} == "i386"
SUBDIR+=binutils cc
.ORDER: binutils cc
.endif

.if !defined(NOPERL) && exists(${.CURDIR}/perl)
SUBDIR+=perl
.endif

.include <bsd.subdir.mk>
