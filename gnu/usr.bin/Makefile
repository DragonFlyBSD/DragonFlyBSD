# $FreeBSD: src/gnu/usr.bin/Makefile,v 1.51 2000/01/16 00:11:34 obrien Exp $
# $DragonFly: src/gnu/usr.bin/Makefile,v 1.3 2003/11/04 15:16:28 eirikn Exp $

SUBDIR= awk bc binutils cc cpio dc dialog diff diff3 gperf \
	grep groff gzip man patch ptx rcs sdiff sort tar texinfo

.if !defined(NO_CVS)
SUBDIR+=cvs
.endif

.if ${MACHINE_ARCH} == "i386"
SUBDIR+=as ld
.endif

.if !defined(NOPERL) && exists(${.CURDIR}/perl)
SUBDIR+=perl
.endif

.include <bsd.subdir.mk>
