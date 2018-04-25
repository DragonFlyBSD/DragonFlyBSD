#	$OpenBSD: Makefile,v 1.4 2016/01/07 16:00:31 tb Exp $
#	$NetBSD: Makefile,v 1.2 1995/03/21 12:14:47 cgd Exp $
#	@(#)Makefile	8.1 (Berkeley) 6/11/93

PROG=	mkdict
CFLAGS+=-I${.CURDIR}/../boggle

build-tools: mkdict.nx

# this mkdict is used in place, it is not installed anywhere
install:

.include <bsd.hostprog.mk>
