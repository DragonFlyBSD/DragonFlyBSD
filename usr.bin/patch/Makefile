#	$OpenBSD: Makefile,v 1.3 2003/07/18 02:00:09 deraadt Exp $
#	$DragonFly: src/usr.bin/patch/Makefile,v 1.3 2004/10/11 21:36:48 dillon Exp $

PROG=	patch
SRCS=	patch.c pch.c inp.c util.c backupfile.c

.if defined(BOOTSTRAPPING)

.PATH:	${.CURDIR}/../../lib/libc/stdlib
SRCS+=	getopt.h getopt_long.c
CFLAGS+= -I${.OBJDIR} -DBOOTSTRAPPING

getopt.h : ${.CURDIR}/../../include/getopt.h
	cp ${.ALLSRC} ${.TARGET}

.endif

WARNS?=	6

.include <bsd.prog.mk>
