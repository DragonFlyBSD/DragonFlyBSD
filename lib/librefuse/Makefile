# $NetBSD: Makefile,v 1.8 2007/11/05 13:41:52 pooka Exp $

LIB=		refuse

.ifdef DEBUG
FUSE_OPT_DEBUG_FLAGS=	-g -DFUSE_OPT_DEBUG
.endif

SRCROOT=	${.CURDIR}/../..
CFLAGS+=	${FUSE_OPT_DEBUG_FLAGS}
CFLAGS+=	-I${SRCROOT}/lib/libpuffs -I${SRCROOT}/sys -I.
SRCS=		refuse.c refuse_opt.c
MAN=		refuse.3
WARNS=		2
INCS=           fuse.h fuse_opt.h

MOUNT=		${.CURDIR}/../../sbin/mount
.PATH:		${MOUNT}
CFLAGS+=	-I${MOUNT}

.include <bsd.lib.mk>
