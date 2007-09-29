#	$OpenBSD: Makefile,v 1.4 2005/05/16 15:22:46 espie Exp $
#	$DragonFly: src/usr.bin/patch/Makefile,v 1.7 2007/09/29 23:11:10 swildner Exp $

PROG=	patch
SRCS=	patch.c pch.c inp.c util.c backupfile.c mkpath.c

.if defined(BOOTSTRAPPING)
CFLAGS+= -DBOOTSTRAPPING
.endif

.include <bsd.prog.mk>
