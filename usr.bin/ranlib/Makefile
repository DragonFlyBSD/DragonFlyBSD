#	@(#)Makefile	8.1 (Berkeley) 6/6/93
# $FreeBSD: src/usr.bin/ranlib/Makefile,v 1.7.2.1 2001/04/25 11:29:33 ru Exp $
# $DragonFly: src/usr.bin/ranlib/Attic/Makefile,v 1.4 2004/01/28 16:41:18 joerg Exp $

PROG=	ranlib
SRCS=	archive.c build.c misc.c ranlib.c touch.c
CFLAGS+=-I${.CURDIR} -I${.CURDIR}/../ar
MAN=	ranlib.1aout ranlib.5
BINDIR= /usr/libexec/binutils212/aout
VPATH=	${.CURDIR}/../ar

.include <bsd.prog.mk>
