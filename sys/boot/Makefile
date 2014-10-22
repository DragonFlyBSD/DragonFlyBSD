# $FreeBSD: src/sys/boot/Makefile,v 1.18 2003/08/16 02:48:20 obrien Exp $

.if ${MACHINE_ARCH} == "x86_64"
.MAKEFLAGS:  MACHINE_ARCH=i386 MACHINE_PLATFORM=pc32 REALLY_X86_64=true
.else
.MAKEFLAGS:  REALLY_X86_64=false
.endif

# Build DLoader
SUBDIR+=		dloader

# Pick the machine-dependant subdir based on the target architecture.
.if ${REALLY_X86_64} == "true"
SUBDIR+=		pc32
.else
SUBDIR+=		${MACHINE_PLATFORM}
.endif

.include <bsd.subdir.mk>
