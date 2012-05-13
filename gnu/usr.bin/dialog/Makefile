# Makefile for dialog
# $FreeBSD: src/gnu/usr.bin/dialog/Makefile,v 1.8.2.1 2001/04/25 12:03:34 ru Exp $

SUBDIR=	TESTS

PROG=   dialog
WARNS?=	3

DPADD+= $(LIBDIALOG) $(LIBNCURSES)
LDADD+= -ldialog -lncurses

.include <bsd.prog.mk>
