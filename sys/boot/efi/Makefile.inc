# $FreeBSD: head/sys/boot/efi/Makefile.inc 287227 2015-08-27 23:46:42Z imp $

BINDIR?=	/boot

.if ${MACHINE_CPUARCH} == "i386"
CFLAGS+=        -march=i386
CFLAGS+=	-mno-aes
.endif

# Options used when building app-specific efi components
# See conf/kern.mk for the correct set of these
CFLAGS+=	-ffreestanding -Wformat -msoft-float ${CFLAGS_NO_SIMD}
LDFLAGS+=	-nostdlib

.if ${MACHINE_CPUARCH} == "amd64"
CFLAGS+=	-fshort-wchar
CFLAGS+=	-mno-red-zone
CFLAGS+=	-mno-aes
.endif

.include "../Makefile.inc"
