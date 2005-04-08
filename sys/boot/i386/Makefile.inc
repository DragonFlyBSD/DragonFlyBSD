# Common defines for all of /sys/boot/i386/
#
# $FreeBSD: src/sys/boot/i386/Makefile.inc,v 1.4 2003/06/26 03:51:57 peter Exp $
# $DragonFly: src/sys/boot/i386/Attic/Makefile.inc,v 1.6.2.1 2005/04/08 00:45:22 dillon Exp $

LOADER_ADDRESS?=	0x200000
CFLAGS+=		-ffreestanding -mpreferred-stack-boundary=2
.if ${CCVER} == "gcc34"
CFLAGS+=		-fno-stack-protector
.endif

.if ${MACHINE_ARCH} == "amd64"
.MAKEFLAGS:  MACHINE_ARCH=i386 MACHINE=i386 REALLY_AMD64=true
.endif

.if defined(REALLY_AMD64) && !defined(__been_to_Makefile_inc)
__been_to_Makefile_inc=	1
CFLAGS+=		-m32
LDFLAGS+=		-m elf_i386_fbsd
AFLAGS+=		--32
.endif
