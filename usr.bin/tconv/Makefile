# Makefile for tconv
# $FreeBSD: src/usr.bin/tconv/Makefile,v 1.5 1999/08/28 01:06:17 peter Exp $
# $DragonFly: src/usr.bin/tconv/Attic/Makefile,v 1.2 2003/06/17 04:29:32 dillon Exp $

PROG=   tconv
SRCS=   tconv.c quit.c
MLINKS= tconv.1 tic.1  tconv.1 captoinfo.1
LINKS=  ${BINDIR}/tconv ${BINDIR}/tic  ${BINDIR}/tconv ${BINDIR}/captoinfo
CFLAGS+= -I${.CURDIR}/../../lib/libmytinfo -Wall
DPADD= $(LIBMYTINFO)
LDADD= -lmytinfo

.include <bsd.prog.mk>
