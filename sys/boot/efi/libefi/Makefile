# $FreeBSD: head/sys/boot/efi/libefi/Makefile 294028 2016-01-14 18:46:57Z smh $

LIB=	efi
INTERNALLIB=
WARNS?=	2

SRCS=	delay.c efi_console.c efinet.c efipart.c errno.c handles.c \
	libefi.c time.c

.if ${MACHINE_ARCH} == "x86_64"
CFLAGS+= -fPIC -mno-red-zone
CFLAGS+= -I${.CURDIR}/../../../contrib/edk2/Include/X64
.endif
CFLAGS+= -I${.CURDIR}/../include
CFLAGS+= -I${.CURDIR}/../../../contrib/edk2/Include
CFLAGS+= -I${.CURDIR}/../../../../lib/libstand

# Pick up the bootstrap header for some interface items
CFLAGS+= -I${.CURDIR}/../../common

# Disable stack protector
CFLAGS+= -fno-stack-protector

CFLAGS+= -DTERM_EMU

.include <bsd.lib.mk>
