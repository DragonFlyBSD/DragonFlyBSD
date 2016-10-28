# $FreeBSD: src/gnu/usr.bin/Makefile,v 1.51 2000/01/16 00:11:34 obrien Exp $

SUBDIR= dmsetup lvm

.if !defined(NO_CRYPT)
SUBDIR+= cryptsetup
.endif

# maximum parallelism
#
SUBDIR_ORDERED=

.include <bsd.subdir.mk>
