#	$OpenBSD: Makefile,v 1.2 2003/11/03 19:51:42 otto Exp $
#	$DragonFly: src/usr.bin/bc/Makefile,v 1.4 2007/08/27 16:50:51 pavalos Exp $

PROG=		bc
SRCS=		bc.y scan.l y.tab.h
CFLAGS+=	-I. -I${.CURDIR}
WARNS?=		2

SCRIPTS=	bc.library
SCRIPTSMODE=	444
SCRIPTSDIR=	/usr/share/misc
SCRIPTSNAME_bc.library= ${SCRIPTS}

.include <bsd.prog.mk>
