#	$OpenBSD: Makefile,v 1.2 2003/11/03 19:51:42 otto Exp $
#	$DragonFly: src/usr.bin/bc/Makefile,v 1.2 2004/10/23 14:07:31 joerg Exp $

PROG=		bc
SRCS=		bc.y scan.l y.tab.h
CFLAGS+=	-I. -I${.CURDIR}
CFLAGS+=	-Wall -Wno-unused

SCRIPTS=	bc.library
SCRIPTSMODE=	444
SCRIPTSDIR=	/usr/share/misc

.include <bsd.prog.mk>
