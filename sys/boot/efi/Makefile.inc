# $FreeBSD: head/sys/boot/efi/Makefile.inc 287227 2015-08-27 23:46:42Z imp $

BINDIR?=	/boot

# Options used when building app-specific efi components
# See conf/kern.mk for the correct set of these
CFLAGS+=	-O
CFLAGS+=	-ffreestanding -Wformat
CFLAGS+=	-mno-mmx -mno-3dnow -mno-sse -mno-sse2 -mno-sse3 -msoft-float
LDFLAGS+=	-nostdlib

.if ${MACHINE_ARCH} == "x86_64"
CFLAGS+=	-fshort-wchar
CFLAGS+=	-mno-red-zone
CFLAGS+=	-mno-aes
.endif
