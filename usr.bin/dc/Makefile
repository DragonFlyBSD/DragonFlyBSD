#	$OpenBSD: Makefile,v 1.1 2003/09/19 17:58:25 otto Exp $
#	$DragonFly: src/usr.bin/dc/Makefile,v 1.2 2004/12/13 21:48:16 joerg Exp $

PROG=	dc
SRCS=	dc.c bcode.c inout.c mem.c stack.c
COPTS+= -Wall
DPADD=	${LIBCRYPTO}
LDADD=	-lcrypto

.include <bsd.prog.mk>
