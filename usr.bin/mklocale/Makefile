#	@(#)Makefile	8.1 (Berkeley) 6/7/93
# $FreeBSD: src/usr.bin/mklocale/Makefile,v 1.21.2.2 2001/04/25 11:29:28 ru Exp $
# $DragonFly: src/usr.bin/mklocale/Makefile,v 1.4 2005/04/21 16:36:35 joerg Exp $

PROG=		mklocale
SRCS=		yacc.y lex.l
CFLAGS+=-I. -I${.CURDIR} -I${.CURDIR}/../../lib/libc
YFLAGS=	-d
WARNS?=	6

.include <bsd.prog.mk>
