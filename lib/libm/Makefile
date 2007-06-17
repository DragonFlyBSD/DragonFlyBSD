# $DragonFly: src/lib/libm/Makefile,v 1.7 2007/06/17 17:46:01 pavalos Exp $

LIB=		m
SHLIB_MAJOR=	3
WARNS?=		6

# Don't MD fragment must be included before MI fragment.
ARCHDIR?= ${MACHINE_ARCH}
.if ${MACHINE_ARCH} == "amd64"
ARCHDIR= i386
.endif

.if exists(arch/${ARCHDIR}/Makefile.inc)
.include "arch/${ARCHDIR}/Makefile.inc"
.endif
.include "gen/Makefile.inc"
.include "man/Makefile.inc"
.include "src/Makefile.inc"

.include <bsd.lib.mk>
