#	$OpenBSD: Makefile,v 1.1 2003/09/19 17:58:25 otto Exp $
#	$DragonFly: src/usr.bin/dc/Makefile,v 1.3 2007/08/27 16:50:52 pavalos Exp $

PROG=	dc
SRCS=	dc.c bcode.c inout.c mem.c stack.c
WARNS?=	2
DPADD=	${LIBCRYPTO}
LDADD=	-lcrypto

.include <bsd.prog.mk>
