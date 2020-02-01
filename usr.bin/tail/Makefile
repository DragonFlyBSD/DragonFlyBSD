#	@(#)Makefile	8.1 (Berkeley) 6/6/93
# $DragonFly: src/usr.bin/tail/Makefile,v 1.3 2007/08/27 16:50:59 pavalos Exp $

PROG=	tail
SRCS=	forward.c misc.c read.c reverse.c tail.c

.if defined(BOOTSTRAPPING)
CFLAGS+= -DBOOTSTRAPPING
.endif

.include <bsd.prog.mk>
