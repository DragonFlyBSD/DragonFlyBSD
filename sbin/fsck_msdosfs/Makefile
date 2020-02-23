# $FreeBSD$

FSCK=	${.CURDIR}/../fsck
.PATH: ${FSCK}

PROG=	fsck_msdosfs
MAN=	fsck_msdosfs.8
SRCS=	main.c check.c boot.c fat.c dir.c fsutil.c
WARNS?=	2

CFLAGS+= -I${FSCK}
DPADD=		${LIBUTIL}
LDADD=		-lutil

.include <bsd.prog.mk>
