# $FreeBSD: src/release/picobsd/tinyware/view/Makefile,v 1.3 1999/08/28 01:34:01 peter Exp $
# $DragonFly: src/release/picobsd/tinyware/view/Attic/Makefile,v 1.2 2003/06/17 04:27:21 dillon Exp $

PROG=view
SRCS=view.c
CFLAGS+=-I/usr/local/include
LDADD+=-L/usr/local/lib -lpng -lvgl -lz -lm
NOMAN=yes

.include <bsd.prog.mk>
