# $FreeBSD: src/sys/boot/Makefile,v 1.18 2003/08/16 02:48:20 obrien Exp $
# $DragonFly: src/sys/boot/Makefile,v 1.5 2007/05/18 07:41:40 dillon Exp $

.if ${MACHINE_ARCH} == "x86_64"
.MAKEFLAGS:  MACHINE_ARCH=i386 MACHINE_PLATFORM=pc32 REALLY_X86_64=true
.endif

# Build DLoader
SUBDIR+=		dloader

# Build OpenFirmware library
.if ${MACHINE_ARCH} == "powerpc" || ${MACHINE_ARCH} == "sparc64"
SUBDIR+=		ofw
.endif

# Pick the machine-dependant subdir based on the target architecture.
SUBDIR+=		${MACHINE_PLATFORM}

# Build EFI executable on ia64
.if ${MACHINE_ARCH} == "ia64"
SUBDIR+=		efi
.endif

.include <bsd.subdir.mk>
