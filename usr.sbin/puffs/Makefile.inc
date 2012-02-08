MOUNT=	${.CURDIR}/../../../sbin/mount

.PATH:	${MOUNT}

WARNS?=	2

CFLAGS+=-I${MOUNT} -I${.CURDIR}/../../../lib/libpuffs -I${.CURDIR}/../../../sys

LDADD=	-lpuffs
DPADD=	${LIBPUFFS}

.include "${.CURDIR}/../../Makefile.inc"
