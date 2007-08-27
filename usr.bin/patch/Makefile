#	$OpenBSD: Makefile,v 1.3 2003/07/18 02:00:09 deraadt Exp $
#	$DragonFly: src/usr.bin/patch/Makefile,v 1.6 2007/08/27 16:50:57 pavalos Exp $

PROG=	patch
SRCS=	patch.c pch.c inp.c util.c backupfile.c

.if defined(BOOTSTRAPPING)
CFLAGS+= -DBOOTSTRAPPING
.endif

.include <bsd.prog.mk>
