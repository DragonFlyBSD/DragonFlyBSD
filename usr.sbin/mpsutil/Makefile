# $FreeBSD: head/usr.sbin/mpsutil/Makefile 330790 2018-03-12 05:03:32Z scottl $

PROG=	mpsutil
SRCS=	mps_cmd.c mps_debug.c mps_flash.c mps_show.c mpsutil.c
MAN=	mpsutil.8

WARNS?= 3

#LIBADD=	cam util
LINKS=	${BINDIR}/mpsutil ${BINDIR}/mprutil
MLINKS=	mpsutil.8 mprutil.8

CFLAGS+= -I${.CURDIR}/../../sys -I. -DUSE_MPT_IOCTLS
# Avoid dirdep dependency on libutil
CFLAGS+= -I${.CURDIR}/../../lib/libutil

# Here be dragons
.ifdef DEBUG
CFLAGS+= -DDEBUG
.endif

.include <bsd.prog.mk>
