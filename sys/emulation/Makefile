# $DragonFly: src/sys/emulation/Makefile,v 1.4 2008/07/23 16:39:34 dillon Exp $
#

.if ${MACHINE_ARCH} == i386
SUBDIR=linux
.elif ${MACHINE_ARCH} == x86_64
SUBDIR=
.else
.error Unknown MACHINE_ARCH.
.endif

# needs to be ported to new net80211 stack
# SUBDIR= ndis

.include <bsd.subdir.mk>
