#	@(#)Makefile	8.1 (Berkeley) 6/4/93
# $FreeBSD: src/lib/libcompat/Makefile,v 1.19.2.1 2001/04/25 10:04:23 ru Exp $

LIB=compat
NOPIC=
WARNS?=	2

.PATH:	${.CURDIR}/4.1 ${.CURDIR}/4.3 ${.CURDIR}/4.4

# compat 4.1 sources
SRCS+=	ftime.c gtty.c stty.c

MAN+=	4.1/ftime.3 4.1/stty.3

MLINKS+=stty.3 gtty.3

# compat 4.3 sources
SRCS+=	re_comp.c rexec.c
SRCS+=	insque.c remque.c

MAN+=	4.3/insque.3 4.3/re_comp.3 4.3/rexec.3

MLINKS+=insque.3 remque.3
MLINKS+=re_comp.3 re_exec.3

# compat 4.4 sources
SRCS+=	cuserid.c
MAN+=	4.4/cuserid.3

.include <bsd.lib.mk>
