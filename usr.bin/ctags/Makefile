#	@(#)Makefile	8.1 (Berkeley) 6/6/93
# $FreeBSD: src/usr.bin/ctags/Makefile,v 1.2.6.1 2001/09/18 04:16:53 mikeh Exp $
# $DragonFly: src/usr.bin/ctags/Makefile,v 1.3 2007/08/27 16:50:52 pavalos Exp $

PROG=	ctags
CFLAGS+=-I${.CURDIR}
SRCS=	C.c ctags.c fortran.c lisp.c print.c tree.c yacc.c
WARNS?=	1

.include <bsd.prog.mk>
