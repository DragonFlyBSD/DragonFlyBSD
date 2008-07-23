# $DragonFly: src/sys/emulation/Makefile,v 1.4 2008/07/23 16:39:34 dillon Exp $
#

.if ${MACHINE_ARCH} == i386
SUBDIR=linux ndis
.elif ${MACHINE_ARCH} == amd64
SUBDIR=
.else
.error Unknown MACHINE_ARCH.
.endif

.include <bsd.subdir.mk>
