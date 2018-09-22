# $FreeBSD: src/usr.sbin/zic/zic/Makefile,v 1.5.2.1 2001/04/25 12:11:16 ru Exp $

.PATH:	${.CURDIR}/..

PROG=	zic

SRCS=	zic.c ialloc.c scheck.c
MAN=	${.CURDIR}/../zic.8
CFLAGS+= -I${.CURDIR}/.. -I${.CURDIR}/../../../lib/libc/stdtime

.include <bsd.prog.mk>
