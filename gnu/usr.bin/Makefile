# $FreeBSD: src/gnu/usr.bin/Makefile,v 1.51 2000/01/16 00:11:34 obrien Exp $
# $DragonFly: src/gnu/usr.bin/Makefile,v 1.6 2004/02/02 05:43:10 dillon Exp $
#
# Note that gcc2 is only built under the i386 architecture.  Other
# architectures require gcc3.

SUBDIR= awk bc cpio dc dialog diff diff3 gperf \
	grep groff gzip man patch ptx rcs sdiff send-pr sort tar texinfo

.if !defined(NO_CVS)
SUBDIR+=cvs
.endif

SUBDIR+= binutils214 cc3

.if ${MACHINE_ARCH} == "i386"
SUBDIR+=binutils cc as ld
.endif

.if !defined(NOPERL) && exists(${.CURDIR}/perl)
SUBDIR+=perl
.endif

.include <bsd.subdir.mk>
