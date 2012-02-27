MOUNT=	${.CURDIR}/../../../sbin/mount

.PATH:	${MOUNT}

WARNS?=	2

CFLAGS+=-I${MOUNT}

LDADD=	-lpuffs
DPADD=	${LIBPUFFS}

.include "${.CURDIR}/../../Makefile.inc"
