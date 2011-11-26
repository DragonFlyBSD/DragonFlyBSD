# Makefile for dialog
# $FreeBSD: src/gnu/usr.bin/dialog/Makefile,v 1.8.2.1 2001/04/25 12:03:34 ru Exp $
# $DragonFly: src/gnu/usr.bin/dialog/Makefile,v 1.2 2003/06/17 04:25:45 dillon Exp $

SUBDIR=	TESTS

PROG=   dialog
WARNS?=	3

DPADD+= $(LIBDIALOG) $(LIBNCURSES) $(LIBMYTINFO)
LDADD+= -ldialog -lncurses -lmytinfo

.include <bsd.prog.mk>
