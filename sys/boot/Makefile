# $FreeBSD: src/sys/boot/Makefile,v 1.18 2003/08/16 02:48:20 obrien Exp $

# Build DLoader
SUBDIR+=		libstand32

.if ${MACHINE_ARCH} == "x86_64"
# Build i386 version of libstand
SUBDIR+=		dloader32
.endif

SUBDIR+=		dloader

# Pick the machine-dependant subdir based on the target architecture.
.if ${MACHINE_ARCH} == "x86_64"
SUBDIR+=		pc32
.else
SUBDIR+=		${MACHINE_PLATFORM}
.endif

# UEFI support
SUBDIR+=		efi

.include <bsd.subdir.mk>
