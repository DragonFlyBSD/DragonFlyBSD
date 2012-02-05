PROG=	pnullfs
SRCS=	pnullfs.c getmntopts.c
LDADD+= -lpuffs -lutil

WARNS=	2

NOMAN=	1

MOUNT=	/usr/src/sbin/mount
.PATH:  ${MOUNT}
CFLAGS+= -I${MOUNT}

SRCROOT=${.CURDIR}/../../../..
DEBUG_FLAGS=-g -O0 -I${SRCROOT}/lib/libpuffs -I${SRCROOT}/sys
LDFLAGS+=-L${SRCROOT}/lib/libpuffs

.include <bsd.prog.mk>
