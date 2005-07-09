#	$OpenBSD: Makefile,v 1.3 2003/07/18 02:00:09 deraadt Exp $
#	$DragonFly: src/usr.bin/patch/Makefile,v 1.5 2005/07/09 11:05:10 corecode Exp $

PROG=	patch
SRCS=	patch.c pch.c inp.c util.c backupfile.c
WARNS?=	6

.if defined(BOOTSTRAPPING)
CFLAGS+= -DBOOTSTRAPPING
.endif

.include <bsd.prog.mk>
