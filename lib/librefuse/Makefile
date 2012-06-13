# $NetBSD: Makefile,v 1.8 2007/11/05 13:41:52 pooka Exp $

LIB=		refuse

SRCS=		refuse.c refuse_opt.c
MAN=		refuse.3
WARNS=		2
INCS=		fuse.h fuse_opt.h

.ifdef DEBUG
CFLAGS+=	-g -DFUSE_OPT_DEBUG
.endif

.include <bsd.lib.mk>
